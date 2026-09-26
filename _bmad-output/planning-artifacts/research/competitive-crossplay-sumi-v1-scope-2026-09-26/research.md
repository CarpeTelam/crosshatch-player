---
title: 'competitive research: CrossPlay and SUMI teardown for crosshatch-player v1 scope'
type: 'competitive'
topic: 'CrossPlay and SUMI teardown for crosshatch-player v1 scope'
decision: 'What crosshatch-player v1 should include and where it should diverge from CrossPlay'
source: 'native run'
status: complete
preset: 'standard'
validation: 'normal'
created: '2026-09-26'
updated: '2026-09-26'
claims_verified: 9
claims_unverified: 9
claims_overturned: 1
sources: 72
---

# Competitive research: CrossPlay and SUMI teardown for crosshatch-player v1 scope

**Decision this research serves:** What crosshatch-player v1 should include and where it should diverge from CrossPlay


## Executive summary

**Recommendation.** Scope v1 as a **social pen-and-paper game platform**. The core is one multiplayer model where each player's "seat" can be a person on this device, a person on a nearby device, or the computer. Pass-and-play, Play Nearby, and play with more than two players then become the same feature. On top of it, add a Lua scripting layer that is touch-first and multiplayer-aware.

**Three findings drive this.**

1. **The gap is social play, not catalog size.**
   - **No project for the Sticky or the X4 Pro combines these pieces.**
   - **CrossPlay** has Play Nearby, but it is built for exactly two players [7]. Among its two-player board games, pass-and-play exists only in Chess and Go [3][67], and every game is hand-written C++ of more than 1.2k lines [9][10].
   - **CrossMux** already ships builds for both boards, but its README and apps guide describe only built-in games and mention no scripting or multiplayer [53][54].
   - **Upstream CrossPoint** closed a request for games as not planned [57], and a games fork reports that games are out of scope upstream [55].
   - **Other game forks** converge on overlapping subsets of Sudoku, Minesweeper, 2048, and Chess [52][54][56].
2. **Going beyond two players is a software design job, not a radio limit.** ESP-NOW allows 20 peers and broadcast [34][68]. CrossPlay's limit comes from its session, turn order, and screens, which all assume two seats [7][4]. No turn-based ESP-NOW game for three to eight devices was found [51], so this would be new engineering.
3. **What's worth taking from SUMI is its sandbox recipe, not a library of scripted games.**
   - **The recipe:** Lua 5.4 compiled as C, so it needs no C++ exceptions, plus a per-script heap cap, an instruction limit, trimmed libraries, and a jailed data folder [22][23][24][25][70]. SUMI is MIT-licensed [21].
   - **What SUMI lacks:** it targets button-only ESP32-C3 devices, ships no Lua games, and has been quiet since May [21][29][31]. Its script API has no touch input, 1-bit drawing only, no refresh control, and no multiplayer [24][27][28].
   - So most of crosshatch-player's scripting API would be new design.

**Biggest caveat.** Demand is not evidenced. Reddit was unreachable from the research environment, CrossPlay has 12 issues and none are about multiplayer [19], and no usage data exists for any e-ink game. The radio's battery cost during a match is also unmeasured, even by CrossPlay [5].

**Where v1 diverges from CrossPlay.** Each point restates evidence from the sections below.

| | CrossPlay | crosshatch-player v1 |
|---|---|---|
| Players | Two seats, a single peer [7] | Two to eight seats, each local, nearby, or the computer |
| Pass-and-play | Chess and Go only [3] | Every turn-based game, with a full-refresh hand-off for hidden information [48] |
| Adding a game | Hand-written C++, more than 1.2k lines [9][10] | A Lua script, or C++ for first-party games |
| Fork shape | 169 upstream files modified [12] | Purely additive, behind device and capability guards |
| Play with CrossPlay devices | Not applicable | Not in v1 (see Open questions) |

## 1. CrossPlay teardown

**Takeaway.** CrossPlay proves that Play Nearby works on these boards, and its link layer has patterns worth borrowing. But its multiplayer is two-player throughout, and every game is hand-written C++. It is MIT-licensed [16]. Except where noted, this section is read from its source at `xteink@31d94db1` (2026-09-22) [1].

### Catalog and modes

**Catalog.** The launcher registers 22 games [1]. The board, puzzle, and abstract ones are:
- Connect Four
- Checkers
- Chess
- Go (9×9 and 13×13)
- Battleship
- Minesweeper
- Sudoku
- Picross and D&Diagrams
- Murdle
- Yahtzee
- Knucklebones

It ships none of the classic pen-and-paper games: tic-tac-toe, dots-and-boxes, hangman, SOS, or mancala. Guess Who exists only as a design doc [1].

**Multiplayer modes.**
- **Play Nearby** covers ten games: Chess, Connect Four, Battleship, Jaipur, Checkers, Yahtzee, Knucklebones, Sea Salt, Toy Battle, and Go [2].
- **Pass-and-play on one device.** Among the two-player board games, only Chess and Go offer it [3][67]:
  - **Chess** has Computer, Pass-and-play, and Face-to-face modes, and rotates the board in pass-and-play.
  - **Go** has Computer and Human modes.

  The other eight Play Nearby games are solo or against the computer when played on one device (confidence: medium, found by searching the code) [3].
- **Party games on one shared device.** Forehead, Wavelength, and Insider are played by handing one device around. Insider takes four to eight people, and Wavelength shows a "PASS THE DEVICE" screen. Trivia is read aloud to the room [4][67].

### How Play Nearby works

- **Radio.** It uses ESP-NOW on fixed Wi-Fi channel 1, with no router. It takes over Wi-Fi, so a match can't run alongside a Wi-Fi connection [5].
- **Discovery.** Devices find each other through a broadcast Hello, and a coin toss decides who moves first [5][7].
- **Messages.** Each packet is a custom binary header plus a payload of at most 192 bytes, for at most 204 bytes in total. That keeps the packet under ESP-NOW v1's 250-byte limit. The payload is the raw bytes of a C++ struct, so both devices must run the same build [6].
- **Keeping in sync.** Each turn sends the whole game state, not the move. An unacknowledged state is resent every 400 ms, and a silent peer is dropped after 10 s. There is no reconnect or resume [7].
- **No security.** Nothing is encrypted or authenticated, so the design assumes everyone in the room is cooperating [5].

**It is built for exactly two players.**
- **The session.** It stores one peer and uses one true/false flag for whose turn it is and another for who hosts [7].
- **Three devices.** Pairing pairs them off two at a time instead of forming a group [7].
- **The screens.** The lobby screen draws two seats [4].
- **Hearts.** It is one human against three computer players, and it doesn't offer Play Nearby because "the radio seats two" [4].

The ESP-NOW radio itself allows 20 peers [34]. So going past two players means redesigning the session, the turn order, and the screens; it is not a matter of changing a constant [7].

**Two engineering choices are worth borrowing:**
- **Nothing is allocated when a message arrives.** Incoming packets go into a fixed six-slot buffer.
- **The link logic runs without hardware.** It has no Arduino dependency, so host tests can play thousands of matches over a fake link that drops, delays, and reorders packets [8].

### Adding a game and tooling

**Adding a game.** Every game is hand-written C++. A multiplayer game implements about 12 methods of a shared `LinkActivity` base class, keeps its state in 192 bytes or less, and gets a game ID plus one line in the launcher's game list. The link layer supplies the searching, disconnect, and rematch screens [9]. Each game directory has more than 1.2k non-blank lines. The simplest take about 1.3k–1.5k, and Chess and Go take about 3k–4k [10]. There is no scripting or data-driven way to define a game [9].

**Browser build.** A WebAssembly build of the real firmware runs in a browser, and two browser instances can play each other [15].

**Hardware use.** On the Sticky, the fork turns on PSRAM (`-DBOARD_HAS_PSRAM`), which upstream leaves off. Go's search tree lives in PSRAM [14].
- **Refresh timing.** Partial refreshes take about 0.3 s and full refreshes take 1–2 s. Chess spends exactly two partial refreshes per move [14].
- **Battery.** CrossPlay estimates that a game's display refreshes cost a fraction of a percent of the battery [14]. The radio's cost is marked unmeasured in its code [5].

### Launcher scaling and fork maintenance

**Launcher scaling.** The launcher broke down somewhere between 10 and 16 games: it had a silent cap of 16 items, and items below the fold couldn't be reached by touch. The fix was a paged folder [11].

**Fork maintenance.**
- **Development process.** Games are built by LLM agents, with "critic" agents checking rules, looks, and play [11].
- **Drift from upstream.** Measured against the last merged upstream commit, the fork adds 1,430 files and modifies 169 upstream files [12].
- **Merges.** A daily agent routine is set up to merge upstream. One such merge silently deleted a piece of fork code, and the maintainer then added a check that every fork-added line is still present [13].

### Momentum and users

- **Size:** 74 stars and 16 forks at access [17].
- **Releases:** frequent and cut by a bot, but the recent ones are about "Live", a phone-to-device picture app [18].
- **Multiplayer activity:** the last link-layer commits in the fetched history are from 2026-09-12, when Go was added to Play Nearby [18] (confidence: medium, since the clone goes back only 200 commits).
- **User feedback is thin:** 12 issues, none about multiplayer. The only game request (#202) asks for word lists for party games [19].
- **Roadmap:** the maintainer's own records show that 1 of 378 work cards came from a user other than the maintainer. Most were found by LLM agents in audits and reviews [20].

## 2. SUMI teardown: what its Lua system actually is

**Takeaway.** SUMI's Lua support is a well-built sandbox with a thin API and no scripted games. What's worth borrowing is the sandbox recipe and its MIT-licensed code.

### What it is

SUMI is MIT-licensed firmware for the ESP32-C3 Xteink X4 and X3. Those devices have buttons and no touchscreen, so SUMI does not target crosshatch-player's boards [21]. Its lineage is CrossPoint → Papyrix → SUMI [21].

### The engine

- **Lua build.** It uses stock PUC Lua 5.4.7, compiled as C with `LUA_32BITS`. It connects Lua to the firmware with hand-written C functions, not a binding library [22].
- **Error handling.** Every callback into a script runs through `lua_pcall`. Because Lua is compiled as C, errors use setjmp/longjmp instead of C++ exceptions [23][38]. That is the pattern that works with crosshatch-player's `-fno-exceptions` build.
- **Trimmed libraries.** Lua's startup list of libraries is cut to base, table, string, math, and utf8. So `require`, `io`, `os`, `debug`, and coroutines don't exist, and game logic must be written as explicit state machines [25].

### Limits and sandboxing

- **Memory and time.** A custom allocator caps each script's VM at 40 KB of regular heap. Scripts are at most 16 KB of source, and a hook raises an error after 100,000 VM instructions in one callback [23].
- **Errors.** Any error shows a "Lua Error" screen instead of crashing the device [23].
- **Files.** Scripts can read and write only under `/custom/<name>_data/`. Path traversal is rejected, and writes are atomic [24].
- **Removed globals.** `load`, `dofile`, and the `raw*` functions are set to nil [24].

### How scripts plug in

- **Discovery.** The firmware scans `/custom/*.lua` (flat, no subfolders) and loads at most eight scripts. There is no manifest or icon; a script's display name comes from its filename [26].
- **Callbacks.** A script must define `draw()` and `onButton(btn)`. `init(w,h)` is optional, and so is `update()`, which runs at 10 Hz [23].
- **API.** It has 46 flat globals: 1-bit drawing primitives, text, seven UI helpers, `millis` and `random`, sandboxed file functions, time, and battery [24].
- **What's missing.** There is no touch input, and no button-release events, bitmaps, sprites, or greyscale (the absence of touch was found by searching the code) [24]. Scripts can't control the e-ink refresh: the host redraws everything on every render and forces a full refresh every 30 renders [27].
- **Networking.** The only networking is a BLE "bridge" that swaps JSON messages of up to 480 bytes with a web page. There is no device-to-device or multiplayer support [28].

### What ships in Lua

- **Games.** Every game SUMI ships is C++: Chess, Sudoku, Minesweeper, Checkers, 2048, and a Game Boy emulator. The only Lua example in the repo is an 86-line doorbell script [29].
- **Author tooling.** Authoring support is an LLM prompt to copy and paste, plus a browser playground running Fengari. Fengari is Lua 5.3, not the device's 5.4, and it doesn't simulate file I/O. There is no hot reload, and installing a script requires a reboot [30].
- **Sharing.** There is no gallery or sharing mechanism [30].
- **Docs vs. firmware.** The repo docs describe a `sumi.*` namespace and `os.time()`, but the firmware exposes neither. A file-I/O bug report (#20) was closed without a reply [32].

### Maturity

It has one author and 47 commits, each a release dump. The last commit was 2026-05-30, about four months before this research, and it has about 180 stars [31]. PocketInk calls it "the only genuine on-device scriptable plugins in the scene" [33].

## 3. Feasibility on ESP32-S3: hardware, scripting engines, multi-device radio, pass-and-play

**Takeaway.** Both boards have 8 MB of PSRAM, which gives room for a scripting VM. Lua compiled as C works under `-fno-exceptions`. ESP-NOW broadcast can carry more than two players. And hidden-information hand-offs need a full e-ink refresh.

### Target hardware

- **reTerminal Sticky** [63]:
  - ESP32-S3R8 with 8 MB PSRAM and 32 MB flash.
  - 3.97" 800×480 display with 4 grey levels.
  - Capacitive touch.
  - Wi-Fi 4 and BLE 5.0.
  - 750 mAh battery.
- **X4 Pro:**
  - 4.3" touchscreen at 219 PPI, a 1,100 mAh battery, and pogo-pin charging [62].
  - The vendor doesn't disclose its chip; reports say ESP32-S3 [62].
  - Upstream CrossPoint's `x4pro` build targets an ESP32-S3 board profile with 16 MB flash and 8 MB octal PSRAM, and turns PSRAM on [71]. A third-party issue agrees [65]. Both sources describe build settings, not a datasheet.
- **Refresh in practice:** a hands-on review calls the X4 Pro's touch responsive and its refresh "fast enough that it's not bothersome" [64].

### Scripting engines

| Engine | Footprint (vendor-reported) | Works with `-fno-exceptions` | License | ESP32 track record | Watch-outs |
|---|---|---|---|---|---|
| **Lua 5.4** (compiled as C) | Not measured on ESP32 in this run. SUMI caps each VM at 40 KB [23]. NodeMCU reports as little as 17 KB of free RAM for apps (Lua 5.1, ESP8266) [46] | Yes, when compiled as C (setjmp/longjmp) [38][70][23] | MIT [45] | SUMI [21], NodeMCU [46], Espressif's own IDF component guide [45] | A Lua error jumps over C++ stack frames without running their destructors. sol2 without exceptions can't catch errors from bound C++ functions [39] |
| **Berry** | Core under 40 KB of code [40]. "Less than 4KiB heap" on a Cortex-M4 [72]. On ESP32, Tasmota says RAM "starts at ~10KB" (unverified by a second source), and it uses PSRAM [41] | Not confirmed in this run | MIT [40][72] | Tasmota, a large ESP32 deployment [41] | Tasmota advises scripts never block for more than 50 ms [41]. Much smaller author community than Lua |
| **MicroPython** (embed port) | 8 KB heap in the official example [44] | Likely, since it's a C library (not confirmed in this run) | MIT | Many ESP32 users, but the embed port is young | The host must supply the correct C stack top for its garbage collector [44] |
| **wasm3** | About 64 KB of code and 10 KB of RAM [42] | Likely, since it's written in C (not confirmed in this run) | MIT | Supports ESP32 | In "minimal maintenance" [42] |
| **Elk (JS)** | About 20 KB of flash [43] | Likely, since it's written in C (not confirmed in this run) | **AGPLv3 or commercial** [43] | Small | No arrays or closures [43]. Unsuitable |

The engine comparison rests on vendor-reported numbers. No benchmark comparing these engines on the S3 was found (see Open questions).

### Radios for multiplayer

**ESP-NOW.**
- **Peers:** at most 20. Some can be encrypted: the default is six or seven (the header and the docs differ), and the configurable maximum is 17 [34][68].
- **Payload size:** 250 B in v1 and 1,470 B in v2. A v1 device receives only v2 packets of 250 B or less [34][68].
- **Arduino-ESP32 support:** it exposes v2 through `getVersion()` and `getMaxDataLen()`. That was added in PR #11524, first shipped in Arduino-ESP32 3.2.1, and listed again in 3.3.0 [69].
- **Broadcast:** supported. It needs no pairing with each receiver, only adding the broadcast address once as a peer [34].
- **Delivery:** acknowledged only at the radio (MAC) layer, so games need their own sequence numbers and acks [34].

**SoftAP.** When a device acts as a hotspot (SoftAP), its station slots and encrypted ESP-NOW peers share the same hardware key slots, so each reduces what's left for the other [35]. SoftAP `max_connection` has been configured to 15 on S3, but no run with 15 connected stations was found (confidence: low) [47].

**BLE.** At most 10 BLE activities in total: connections, scanning, and advertising combined, each using about 828 B. The NimBLE host defaults to three connections [36][37]. The 10-activity limit comes from the configuration shared by C3 and S3; that it applies to S3 is inferred (confidence: medium) [36].

**What exists already.** Public ESP32 multiplayer projects are almost all two-player, or a hotspot that phones join through a browser. No turn-based ESP-NOW game for three to eight devices was found [51].

*Inference:* ESP-NOW broadcast for discovery, with one host device keeping the authoritative game state, is the best fit for three to eight players on these boards. That's also the direction CrossPlay's existing design would extend in.

### Pass-and-play

**Existing designs.**
- **Tabletopia:** its hotseat mode hides the current player's hand at "End Turn" [50].
- **DuelBox:** its spec requires a full blackout "pass to <player>" screen and "no leak of hidden state in any frame during hand-off". Each game opts in through its manifest [49].
- **Mobile apps:** Nature ships pass-and-play for up to four players, and Ticket to Ride also offers it (confidence: medium) [66].

**E-ink ghosting leaks hidden information.** On a Kobo, typed password keys stayed faintly visible after a partial refresh until the next full refresh [48]. *Inference:* a hand-off screen that hides information has to use a full refresh.

## 4. The wider field and the game catalog

**Takeaway.** One competitor (CrossMux) already covers both boards with single-player built-in games. Beyond CrossPlay, no fork documents local multiplayer. KOReader shows that a large Lua game ecosystem on e-ink is possible.

### Competitors

- **CrossMux** (0x1abin, 219 stars) is a CrossPoint fork already publishing nightly builds for **the Sticky and the X4 Pro**. It says the target list "is not a claim that every feature has passed hardware acceptance" [53].
  - **Games:** its apps are compiled in and listed in one table: Sudoku, Gomoku, Minesweeper, 2048, Chinese Chess, Sokoban, and others. Most games are hidden on a fresh install.
  - **Not documented:** its README and apps guide mention no scripting or multiplayer. Whether Gomoku and Chinese Chess can be played by two people is not documented [54].
- **CrossInk Games Edition** runs only on the X4 Pro. It was created 2026-09-14 and has one star. It notes that games are "deliberately out of scope" in both CrossInk and CrossPoint [55].
- **Upstream CrossPoint** closed issue #678 ("Add Quick Mental Games") as not planned, with no maintainer comment [57]. Games have to live in forks.
- **Other Xteink forks** (CrossPet, noah-ing/X4) converge on overlapping subsets of Sudoku, Minesweeper, 2048, and Chess [52][56].
  - *Inference:* apart from CrossPlay, no Xteink fork found documents local multiplayer, pass-and-play, or human-vs-human pen-and-paper games such as dots-and-boxes (confidence: medium) [52][54][56].
- **KOReader** is the precedent for scripted games on e-ink [58]:
  - One index lists 70 Lua plugins, about 66 of them games.
  - Titles include Battleship, Connect4, Hangman, Mastermind, Gomoku, backgammon, and about 20 pencil-puzzle variants.
  - It has four single-device "Party" games (Boggle, Pictionary, Quiz, Taboo).
  - It also has a shared game library and an on-device plugin manager.

### Borrowing puzzles

Simon Tatham's Portable Puzzle Collection is MIT-licensed [59] and has 41 puzzles, all single-player [60].
- **PocketBook port:** it needed rework for each game: greyscale instead of colour, reworked dragging, and thicker lines [60].
- **Kindle port:** some games need right-click or a keyboard [61].
- **ESP32:** our search found no port.

### Candidate catalog

This is derived from game rules, not usage data, so confidence is low.

| Fit | Games | Players | Hidden information needs |
|---|---|---|---|
| Pen-and-paper, open information | Tic-tac-toe, Ultimate tic-tac-toe, Dots and Boxes, SOS, Sprouts, Nine Men's Morris, Connect Four\*, Gomoku, Othello, Checkers\* | Two; Dots and Boxes and SOS work with three or more | None. Pass-and-play is trivial |
| Hidden information | Battleship\*, Hangman, Mastermind / Bulls and Cows, most card games | Two; Hangman can rotate the setter among three or more | Play Nearby, or a pass-and-play hand-off with a full refresh |
| Solo puzzles (Tatham, MIT) | Sudoku, nonograms, Minesweeper, Mastermind (Guess), Loopy, Bridges | One | None |
| Poor fit | Real-time games (Snake, Tetris, Flappy), drag-heavy puzzles | — | — |

\* Already in CrossPlay [1].

## Cross-dimension insights

1. **Pass-and-play, Play Nearby, and more-than-two-player play fit one "seat" model.** CrossPlay built them as three separate mechanisms:
   - per-game opponent settings for pass-and-play [3]
   - a two-player radio link for Play Nearby [7]
   - party games where one device is handed around [4]

   A model where each seat is a person on this device, a person nearby, or the computer would give every game all three modes at once. It would also give hidden-information games one shared hand-off screen with a full refresh [48][49]. *Inference:* no source documents this pattern.
2. **Scripted games and multiplayer fit together naturally.**
   - **How CrossPlay syncs.** It sends the whole game state each turn, capped at 192 B, not individual moves [6][7].
   - **What SUMI shows.** Sending messages from a script works, but only to a web page over BLE [28].
   - *Inference:* a script that declares its game state as a small serializable table could get Play Nearby and pass-and-play from the host without ever touching the radio. It needs a spike to confirm the state fits in the payload (1,470 B with ESP-NOW v2 [69]).
3. **E-ink refresh constrains every area.**
   - CrossPlay timed its 400 ms resend to hide under a repaint [14].
   - SUMI scripts can't control refresh at all [27].
   - Ghosting leaks hidden information [48].
   - The PocketBook port of Tatham's puzzles needed rework for each game [60].

   So refresh policy must be part of the script API (dirty regions and a way to request a full refresh), not left entirely to the host.
4. **Catalog size breaks launchers early.** CrossPlay's launcher broke down between 10 and 16 games [11], and SUMI caps scripts at eight with no manifest [26].
5. **Fork discipline is a competitive factor.** CrossPlay modifies 169 upstream files and has already lost fork code in an automated merge [12][13]. Upstream closed a games request as not planned, and a games fork reports that games are out of scope upstream [55][57]. So game work is likely to stay in the fork permanently.

## Recommendations

Each recommendation opens with the document it feeds and its confidence.

1. **V1 scope: position around social play.**
   *Feeds:* the product brief's positioning and the PRD's differentiation. *Confidence:* medium. The gap among documented competitors is well evidenced [52][53][54]; demand is not [19].
   The pitch would be: "pen-and-paper games you play *together*, on one device or across several nearby, and new games anyone can script."
2. **Build the seat-based multiplayer core first.**
   *Feeds:* the architecture spine. *Confidence:* medium on the radio limits [34][68]; low on reliability with three to eight devices and on battery cost, which nobody has measured [51][5].
   The design would be:
   - **Discovery:** an ESP-NOW broadcast lobby.
   - **State:** one host device holds the authoritative game state and sends it all each turn, with sequence numbers and app-level acks [34][7].
   - **Players:** two to eight seats, each a person on this device, a person nearby, or the computer.
   - **Hand-off:** hidden-information games get a full-refresh hand-off screen [48][49].

   Borrow CrossPlay's proven patterns as ideas, not code: a message buffer that never allocates, link logic that runs without hardware, and a fake-link test harness [8]. Its session code assumes two players throughout, so reuse is limited [7].
3. **Scripting: Lua 5.4 compiled as C, in S3-only code behind a capability guard, with the fork kept purely additive.**
   *Feeds:* the architecture spine, as a constraint. *Confidence:* medium.
   Start from SUMI's MIT sandbox recipe [22][23][24][25]. Then fill its gaps:
   - touch events
   - control over which screen regions refresh
   - greyscale and bitmaps
   - a manifest with an icon, player counts, and supported modes
   - a paged launcher
   - a heap in PSRAM instead of a 40 KB cap (both boards have 8 MB PSRAM [63][71])
   - a seat and state-sync API

   Lua has the ecosystem precedent: SUMI [21], about 66 KOReader Lua games [58], and Espressif's own IDF component [45]. Berry is the credible alternative, with a large ESP32 deployment in Tasmota [41], but its figure of about 10 KB of RAM is unverified. Settle the choice with a footprint spike on the S3 before committing. Keep all game and scripting code in new files so upstream merges stay cheap [12][13].
4. **Author experience beats API breadth.**
   *Feeds:* the PRD's non-functional requirements. *Confidence:* medium.
   SUMI's weak points were a playground running a different Lua version, docs that contradict the firmware, reboot-to-install, and no gallery [30][32]. CrossPlay's in-browser WebAssembly build, with two instances playing each other, is the model to match for testing scripts [15].
5. **First-party v1 catalog.**
   *Feeds:* the PRD's feature list. *Confidence:* low. The catalog is derived from game rules; there is no usage data.
   Pick games that demonstrate each mode:
   - **More than two players:** Dots and Boxes and SOS.
   - **Hidden information:** Hangman and Mastermind / Bulls and Cows, to exercise the hand-off screen and Play Nearby.
   - **Quick two-player games:** tic-tac-toe and Ultimate tic-tac-toe.
   - **Optionally:** Nine Men's Morris and Sprouts.

   Avoid real-time and drag-heavy games [60][61]. Write at least two of these as Lua scripts, to prove the scripting API.
6. **Watch CrossMux.**
   *Feeds:* the product brief's competitor list. *Confidence:* medium, because its current state comes from its docs, not its source [53][54].
   It is the nearest competitor on the same hardware and has more stars than CrossPlay [53][17]. Recheck it by 2026-12-25 for any move into multiplayer or scripting.

## Open questions

| Question | Why it matters | How to answer |
|---|---|---|
| Do X4 Pro and Sticky owners want multiplayer or scripting? | Recommendation 1 rests on a competitive gap, not demonstrated demand | Run a user-voice research pass from a network that can reach Reddit (r/xteink, r/eink), or post a short poll |
| What are Lua's and Berry's real footprint and speed on the S3 with PSRAM? | Recommendation 3; no benchmark on the S3 was found | A one-day spike: run the same small game in both engines and measure heap and frame time |
| Is ESP-NOW reliable with three to eight devices, and what does a match cost in battery? | Recommendation 2; there is no reference project, and CrossPlay never measured the radio's cost [5] | A prototype lobby tested on real devices, plus a fake-link test harness |
| Which Arduino-ESP32 and ESP-IDF versions does this repo's pioarduino build pin? | ESP-NOW v2 (1,470 B payloads) needs Arduino-ESP32 3.2.1 or later [69]. This is also the overdue row in the staleness map | Check `platformio.ini`. This is a project check for the architect, not web research |
| Should crosshatch-player devices be able to play with CrossPlay devices? | CrossPlay's protocol assumes identical builds and raw struct payloads [6] | A product decision. Probably not in v1 |
| What are the X4 Pro's panel resolution and touch controller? | They affect layouts and touch handling | Read the freeink-sdk board config, or measure on the device |
| Do CrossMux's Gomoku and Chinese Chess support two human players? | It would narrow the gap in recommendation 1 | Read its source or install a nightly build |

## Source appendix

| # | Claim or finding it supports | Publisher | Pub date | Accessed | Confidence |
|---|---|---|---|---|---|
| [1] | 22-game launcher list and board-game subset; Guess Who design doc (docs/apps/guesswho.md) | [ma-r-s/crossplay, src/apps_local/Shelf.cpp and README](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/Shelf.cpp) | 2026-09-22 | 2026-09-26 | high |
| [2] | 10 Play Nearby games (GameId enum) | [ma-r-s/crossplay, link/LinkPlay.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkPlay.h) | 2026-09-22 | 2026-09-26 | high |
| [3] | Among two-player board games, pass-and-play only in Chess and Go | [ma-r-s/crossplay, chess/ChessScreens.h and go/GoFlow.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/chess/ChessScreens.h) | 2026-09-22 | 2026-09-26 | high |
| [4] | Single-device party games; Trivia read aloud; Hearts "the radio seats two"; the lobby screen draws two seats | [ma-r-s/crossplay, docs/apps/hearts.md, forehead.md, wavelength.md, link/LinkScreens.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/apps/hearts.md) | 2026-09-22 | 2026-09-26 | high |
| [5] | ESP-NOW on channel 1, takes over Wi-Fi, unencrypted peers; radio battery cost marked unmeasured (LinkRadio.h) | [ma-r-s/crossplay, link/LinkRadio.cpp](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkRadio.cpp) | 2026-09-22 | 2026-09-26 | high |
| [6] | 12-byte header plus 192-byte payload (204 B max); raw struct payload | [ma-r-s/crossplay, link/LinkProtocol.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkProtocol.h) | 2026-09-22 | 2026-09-26 | high |
| [7] | Whole state sent each turn, 400 ms resend, 10 s drop, no resume; single peer and true/false turn flag | [ma-r-s/crossplay, link/LinkSession.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkSession.h) | 2026-09-22 | 2026-09-26 | high |
| [8] | Fixed six-slot receive buffer; link logic runs without hardware; FakeLink host tests | [ma-r-s/crossplay, link/LinkRadio.h and LinkTransport.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkTransport.h) | 2026-09-22 | 2026-09-26 | high |
| [9] | Adding a game: LinkActivity with about 12 methods, state of 192 B or less, one line in the launcher; no scripting | [ma-r-s/crossplay, docs/building-apps.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/building-apps.md) | 2026-09-22 | 2026-09-26 | high |
| [10] | About 1.3–1.5k lines per simple game (line counts measured on the clone) | [ma-r-s/crossplay, src/apps_local](https://github.com/ma-r-s/crossplay/tree/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local) | 2026-09-22 | 2026-09-26 | medium |
| [11] | LLM-agent build cycle; launcher broke at 10–16 games | [ma-r-s/crossplay, docs/games-at-scale.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/games-at-scale.md) | 2026-08-08 | 2026-09-26 | high |
| [12] | 1,430 files added and 169 upstream files modified vs upstream ce2b4fcd (git diff on the clone) | [ma-r-s/crossplay, LOCAL_SCOPE.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/LOCAL_SCOPE.md) | 2026-09-22 | 2026-09-26 | high |
| [13] | Daily agent-run upstream merges; one silently deleted fork code | [ma-r-s/crossplay, docs/workflow/upstream-sync.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/workflow/upstream-sync.md) | 2026-09-22 | 2026-09-26 | high |
| [14] | PSRAM turned on for the Sticky; Go tree in PSRAM (docs/apps/go.md); refresh timings; refresh battery estimate | [ma-r-s/crossplay, docs/building-apps.md and platformio.ini](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/platformio.ini) | 2026-09-22 | 2026-09-26 | high |
| [15] | WebAssembly browser build with two devices playing each other | [ma-r-s/crossplay, README.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/README.md) | 2026-09-22 | 2026-09-26 | high |
| [16] | MIT license | [ma-r-s/crossplay, LICENSE](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/LICENSE) | 2026-09-22 | 2026-09-26 | high |
| [17] | 74 stars, 16 forks | [GitHub, ma-r-s/crossplay repo page](https://github.com/ma-r-s/crossplay) | 2026-09-26 | 2026-09-26 | medium |
| [18] | Release cadence; recent work on the "Live" app; last link-layer commits 2026-09-12 (clone git log) | [GitHub, ma-r-s/crossplay releases](https://github.com/ma-r-s/crossplay/releases) | 2026-09-22 | 2026-09-26 | medium |
| [19] | 12 issues, none about multiplayer; #202 asks for party-game word lists | [GitHub, ma-r-s/crossplay issue #202](https://github.com/ma-r-s/crossplay/issues/202) | 2026-09-13 | 2026-09-26 | medium |
| [20] | 1 of 378 work cards from a non-maintainer user | [ma-r-s/crossplay, docs/workflow/what-mario-reported.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/workflow/what-mario-reported.md) | 2026-09-06 | 2026-09-26 | high |
| [21] | SUMI: MIT, ESP32-C3 X4/X3, buttons only; CrossPoint → Papyrix → SUMI | [psychoplath9450/SUMI, LICENSE and README](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/README.md) | 2026-05-30 | 2026-09-26 | high |
| [22] | PUC Lua 5.4.7 compiled as C, LUA_32BITS, hand-written bindings | [psychoplath9450/SUMI, lib/lua54](https://github.com/psychoplath9450/SUMI/tree/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/lib/lua54) | 2026-05-30 | 2026-09-26 | high |
| [23] | 40 KB VM cap, 16 KB scripts, 100k-instruction hook, lua_pcall, error screen, callbacks | [psychoplath9450/SUMI, src/plugins/LuaPlugin.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/LuaPlugin.cpp) | 2026-05-30 | 2026-09-26 | high |
| [24] | 46-global API; sandboxed file paths; no touch or bitmaps | [psychoplath9450/SUMI, src/plugins/LuaBindings.h](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/LuaBindings.h) | 2026-05-30 | 2026-09-26 | high |
| [25] | linit trimmed: no require, io, os, debug or coroutines | [psychoplath9450/SUMI, lib/lua54/linit.c](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/lib/lua54/linit.c) | 2026-05-30 | 2026-09-26 | high |
| [26] | Flat /custom/*.lua scan, at most 8 scripts, no manifest | [psychoplath9450/SUMI, src/states/PluginListState.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/states/PluginListState.cpp) | 2026-05-30 | 2026-09-26 | high |
| [27] | Host redraws everything; full refresh every 30 renders; no refresh control for scripts | [psychoplath9450/SUMI, src/states/PluginHostState.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/states/PluginHostState.cpp) | 2026-05-30 | 2026-09-26 | high |
| [28] | BLE bridge to a web page with 480 B JSON; no device-to-device play | [psychoplath9450/SUMI, docs/PLUGIN_BRIDGE.md](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/docs/PLUGIN_BRIDGE.md) | 2026-04 | 2026-09-26 | high |
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
| [41] | Berry RAM "starts at ~10KB" (single source), uses PSRAM, don't block more than 50 ms | [Tasmota, Berry docs](https://tasmota.github.io/docs/Berry/) | unknown | 2026-09-26 | medium |
| [42] | wasm3 in minimal maintenance; about 64 KB code and 10 KB RAM | [wasm3, README](https://github.com/wasm3/wasm3) | unknown | 2026-09-26 | high |
| [43] | Elk is AGPLv3 or commercial; no arrays or closures | [Cesanta, elk README](https://github.com/cesanta/elk) | unknown | 2026-09-26 | high |
| [44] | MicroPython embed port; 8 KB heap example; host supplies the stack top | [MicroPython, examples/embedding/main.c](https://raw.githubusercontent.com/micropython/micropython/master/examples/embedding/main.c) | unknown | 2026-09-26 | high |
| [45] | Lua as an ESP-IDF component, MIT, scripts loaded from a filesystem | [Espressif Developer Portal, Lua as an ESP-IDF component](https://developer.espressif.com/blog/using-lua-as-esp-idf-component-with-esp32/) | 2024-10-22 (updated 2026-04-29) | 2026-09-26 | high |
| [46] | NodeMCU Lua 5.1: as little as 17 KB of RAM for apps; LTR saves 20–25 KB | [NodeMCU, Lua developer FAQ](https://nodemcu.readthedocs.io/en/dev-esp32/lua-developer-faq/) | unknown | 2026-09-26 | high |
| [47] | SoftAP max_connection configured to 15 on S3; only one station connected in that report | [espressif/esp-idf, issue #10511](https://github.com/espressif/esp-idf/issues/10511) | 2023-01 | 2026-09-26 | low |
| [48] | E-ink ghosting leaked typed keys until a full refresh | [Good e-Reader](https://goodereader.com/blog/kobo-ereader-news/e-ink-ghosting-effect-can-reveal-sensitive-info-such-as-passwords) | 2024-05-08 | 2026-09-26 | high |
| [49] | Pass-and-play hand-off: full blackout, no hidden state in any frame, opt-in per game through its manifest | [DuelBox, issue #134](https://github.com/DuelBox/DuelBox-Web/issues/134) | 2026-08-19 | 2026-09-26 | high |
| [50] | Hotseat hides the current player's hand at End Turn | [Tabletopia help center](https://help.tabletopia.com/knowledge-base/game-modes-solo-hotseat-online/) | unknown | 2026-09-26 | high |
| [51] | ESP32 multiplayer projects are 2-player or phones-join-a-hotspot | [GitHub hobby projects, e.g. gamebox-esp32](https://github.com/nirinovich/gamebox-esp32) | unknown | 2026-09-26 | medium |
| [52] | About 20 CrossPoint forks listed; game forks converge on the same games; most forks drop BLE on the C3 | [PocketInk, firmware directory](https://pocketink.io/firmware/) | 2026-08-24 | 2026-09-26 | high |
| [53] | CrossMux Nightly builds for Sticky and X4 Pro | [0x1abin/crossmux, README](https://github.com/0x1abin/crossmux) | 2026-09 | 2026-09-26 | high |
| [54] | CrossMux games compiled in, hidden by default; its docs mention no scripting or multiplayer | [0x1abin/crossmux, src/activities/apps/README.md](https://github.com/0x1abin/crossmux/blob/HEAD/src/activities/apps/README.md) | 2026-09 | 2026-09-26 | medium |
| [55] | CrossInk Games Edition X4 Pro-only; games out of scope in CrossInk and CrossPoint | [LegendaryDrogan/crossink-games-edition, README](https://github.com/LegendaryDrogan/crossink-games-edition) | 2026-09-22 | 2026-09-26 | high |
| [56] | noah-ing/X4: Chess, Minesweeper, Snake, 2048 on the C3 | [noah-ing/X4](https://github.com/noah-ing/X4) | 2026-08-20 | 2026-09-26 | medium |
| [57] | CrossPoint #678 (games request) closed as not planned | [crosspoint-reader, issue #678](https://github.com/crosspoint-reader/crosspoint-reader/issues/678) | 2026-02-03 | 2026-09-26 | medium |
| [58] | 70 KOReader Lua plugins (about 66 games), including party games and a plugin manager | [t2ym5u/koreader-plugins](https://github.com/t2ym5u/koreader-plugins) | 2026 | 2026-09-26 | high |
| [59] | Tatham puzzles are MIT | [Simon Tatham, licence](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/licence.html) | unknown | 2026-09-26 | high |
| [60] | 41 single-player puzzles; PocketBook port needed per-game rework (greyscale, reworked dragging) | [SteffenBauer/PocketPuzzles](https://github.com/SteffenBauer/PocketPuzzles) | 2020–2026 | 2026-09-26 | high |
| [61] | Kindle port: some games need right-click or a keyboard | [kbarni/kindlepuzzles](https://github.com/kbarni/kindlepuzzles) | unknown | 2026-09-26 | high |
| [62] | X4 Pro specs; chip not disclosed by the vendor (reported as ESP32-S3) | [CNX Software, X4 Pro](https://www.cnx-software.com/2026/07/23/99-xteink-x4-pro-4-3-inch-touchscreen-ereader-to-support-crosspoint-reader-open-source-firmware/) | 2026-07-23 | 2026-09-26 | medium |
| [63] | reTerminal Sticky: ESP32-S3R8, 8 MB PSRAM, 800×480, touch, BLE 5 | [CNX Software, reTerminal Sticky](https://www.cnx-software.com/2026/07/31/reterminal-sticky-3-97-inch-magnetic-touch-epaper-display-is-supported-by-four-open-source-firmware-projects/) | 2026-07-31 | 2026-09-26 | high |
| [64] | X4 Pro touch is responsive; refresh "not bothersome" | [Abstract Nonsense, X4 Pro review](https://abstractnonsense.xyz/microblog/2026-09-07-xteink-x4-pro-review/) | 2026-09-07 | 2026-09-26 | medium |
| [65] | X4 Pro is an ESP32-S3 with 8 MB PSRAM | [clackups/draftling, issue #40](https://github.com/clackups/draftling/issues/40) | 2026-08-18 | 2026-09-26 | medium |
| [66] | Nature ships pass-and-play for up to four players; Ticket to Ride also offers it (snippet) | [App Store, Nature board game](https://apps.apple.com/us/app/nature-board-game/id6738703558) | unknown | 2026-09-26 | medium |
| [67] | Verification: pass-and-play modes in Chess and Go; Insider (4–8 people) and Wavelength's "PASS THE DEVICE" | [ma-r-s/crossplay, wavelength/WavelengthCore.h and insider/InsiderActivity.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/wavelength/WavelengthCore.h) | 2026-09-22 | 2026-09-26 | high |
| [68] | esp_now.h: 20 total peers, 6 encrypted by default, 250/1470 B payloads | [Espressif, esp-idf esp_now.h](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_wifi/include/esp_now.h) | unknown | 2026-09-26 | high |
| [69] | Arduino-ESP32 ESP-NOW v2 support (PR #11524, merged 2025-06-30; first shipped in 3.2.1, listed again in 3.3.0) | [espressif/arduino-esp32, PR #11524](https://github.com/espressif/arduino-esp32/pull/11524) | 2025-06-30 | 2026-09-26 | high |
| [70] | Lua uses longjmp for errors, or exceptions if compiled as C++ | [Lua.org, Lua 5.4 manual §4.4](https://www.lua.org/manual/5.4/manual.html) | unknown | 2026-09-26 | high |
| [71] | Upstream x4pro build: esp32-s3-devkitc1-n16r8 board profile, octal PSRAM, BOARD_HAS_PSRAM | [crosspoint-reader, platformio.ini (develop)](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/platformio.ini) | 2026-09 | 2026-09-26 | high |
| [72] | Berry is MIT; runs on "less than 4KiB heap" on a Cortex-M4 | [berry-lang docs site](https://berry-lang.github.io/) | unknown | 2026-09-26 | high |

## Staleness map

This was computed with `recon_kit.py staleness` using the pack's freshness windows: features 3 months, trajectory 6, sentiment 12, versions 1, architecture 24, performance and catalog 12. Where a source has no publication date, the access date is used.

| Claim | Class | Published | Recheck by |
|---|---|---|---|
| [69] Arduino-ESP32 ESP-NOW v2 support | version | 2025-06-30 | **overdue**: recheck against the pinned platform (see Open questions) |
| [71] X4 Pro is an ESP32-S3 with 8 MB PSRAM (upstream build config) | version | 2026-09 | 2026-10-26 |
| [31] SUMI quiet since May | trajectory | 2026-05-30 | 2026-11-30 |
| [3] Pass-and-play only in Chess and Go (CrossPlay) | feature | 2026-09-22 | 2026-12-22 |
| [53] CrossMux on Sticky and X4 Pro, no documented multiplayer | feature | 2026-09 | 2026-12-25 |
| [34] ESP-NOW peer and payload limits | feature | unknown (accessed 2026-09-26) | 2026-12-26 |
| [57] Upstream closed a games request | sentiment | 2026-02-03 | 2027-02-03 |
| [18] CrossPlay's momentum shifted to "Live" | trajectory | 2026-09-22 | 2027-03-22 |
| [52] No other Xteink fork documents local multiplayer | catalog | 2026-08-24 | 2027-08-24 |
| [19] CrossPlay user feedback is thin | sentiment | 2026-09-13 (issues to 2026-09-24) | 2027-09-24 |
| [41] Berry uses about 10 KB of RAM on ESP32 | performance | unknown (accessed 2026-09-26) | 2027-09-26 |
| [23] SUMI Lua limits and API | architecture | 2026-05-30 | 2028-05-30 |
| [7] CrossPlay is hard-coded to two players | architecture | 2026-09-22 | 2028-09-22 |

Competitor features move fastest, so run a Refresh before the PRD is finalized if it slips past December 2026.

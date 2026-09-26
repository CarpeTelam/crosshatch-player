# field (wider ecosystem, game catalog, user voice, hardware) — round 1
## Findings
- claim: PocketInk's firmware directory (verified 2026-08-24) lists ~20 CrossPoint forks for X3/X4; only CrossPoint (1.6.0rc) is listed as supporting X4 Pro; every fork row shows X4 Pro "not supported".
  source: https://pocketink.io/firmware/
  publisher: PocketInk (independent affiliate site)
  pub_date: 2026-08-24
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Game-bearing forks in that directory: CrossPet v1.8.4 (MIT; chess vs AI, Sudoku, 2048, Minesweeper, flashcards, Tamagotchi-style chicken fed by reading ~20 pages), SUMI v0.6.3 (~20 offline apps, Lua plugins, emulator), CrossMux v1.5.5 ("apps hub"), CrossPoint Flow v3.0.1 ("Lua plugins + iPod-style UI"); all X3/X4 (ESP32-C3), none X4 Pro.
  source: https://pocketink.io/firmware/ ; https://pocketink.io/firmware/crosspet/
  publisher: PocketInk
  pub_date: 2026-08-24
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: PocketInk says SUMI Lua is "the only installable plugin system" in the Xteink scene; PlusPoint (a from-scratch C++ OS PoC) is often mislabelled as a JS-app platform but has no launcher yet.
  source: https://pocketink.io/firmware/plugins/
  publisher: PocketInk
  pub_date: 2026-08-24
  accessed: 2026-09-26
  confidence: medium (CrossPoint Flow is also listed as "Lua plugins" on the directory page, contradicting "only")
  class: architecture
- claim: CrossMux (0x1abin/crossmux, 219 stars, 43 forks, 1,884 commits) is a CrossPoint fork with built-in apps (Sudoku, Gomoku, Chinese Chess, Minesweeper, 2048, Electronic Woodfish, Ugly Avatar), no scripting framework, and targets many ESP32 e-ink boards including Seeed Sticky, M5Stack Paper Mono, Waveshare ePaper 3.97, Murphy M4, eego A4, Metalio E-Ink 4.
  source: https://github.com/0x1abin/crossmux
  publisher: 0x1abin (GitHub)
  pub_date: unknown (active, 2026)
  accessed: 2026-09-26
  confidence: medium (WebFetch summary of README)
  class: feature
- claim: CrossInk Games Edition (LegendaryDrogan, created 2026-09-14, 1 star) ships X4 Pro (ESP32-S3) binaries adding "games and quality-of-life improvements"; README states CrossInk upstream "deliberately keeps games out of scope".
  source: https://github.com/LegendaryDrogan/crossink-games-edition
  publisher: LegendaryDrogan
  pub_date: 2026-09-22 (last update)
  accessed: 2026-09-26
  confidence: medium
  class: trajectory
- claim: noah-ing/X4 (independent, 2 stars) is ESP32-C3 X4 firmware with Chess (minimax/alpha-beta AI), Minesweeper (3 levels), Snake (turn-based or auto), 2048, Game of Life, button-only input via ADC ladder, and notes "E-Ink refresh is slow - minimize full refreshes".
  source: https://github.com/noah-ing/X4
  publisher: noah-ing
  pub_date: 2026-08-20 (last update)
  accessed: 2026-09-26
  confidence: medium
  class: feature
- claim: A fork called Picoread reportedly adds Flappy Bird and Tetris plus Wikipedia/Gutenberg/RSS apps to X4 (search-engine summary only; repo not located).
  source: WebSearch summary "Xteink X4 firmware games fork" (unattributed snippet)
  publisher: unknown
  pub_date: unknown
  accessed: 2026-09-26
  confidence: low
  class: catalog
- claim: CrossPlant (0xKnowles, 8 stars) merges CrossInk with CrossPet's virtual-pet mechanics; inkboard merges CrossPet + "Biscuit"; crosspet-x3 ports CrossPet to X3 — gamification (pets/streaks) is a recurring fork theme.
  source: GitHub repo search "crosspet xteink" (github.com/0xKnowles/CrossPlant, treetree17/inkboard, PietroMezzaroba/crosspet-x3)
  publisher: GitHub
  pub_date: 2026-05..2026-09
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: CrossPoint upstream issue #678 "Add Quick Mental Games (Sudoku / Chess)" (2026-02-03) was closed as not planned with no maintainer comment visible; 0 reactions.
  source: https://github.com/crosspoint-reader/crosspoint-reader/issues/678
  publisher: crosspoint-reader GitHub
  pub_date: 2026-02-03
  accessed: 2026-09-26
  confidence: medium
  class: sentiment
- claim: Simon Tatham's Portable Puzzle Collection is MIT-licensed (copy/modify/sublicense/sell; keep notice).
  source: https://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/licence.html
  publisher: Simon Tatham
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: license
- claim: X4 Pro: 4.3" E Ink touchscreen, 219 PPI, dual-tone frontlight, ESP32-S3 (not officially disclosed by Xteink; "tentatively confirmed" via GitHub comment; one source says ESP32-S3R8), 2.4 GHz Wi-Fi 4 + Bluetooth, 1,100 mAh, pogo-pin charging (no USB-C), 16 GB microSD, 111x69x5.95 mm, 72 g; buttons Power/Reset/Prev/Next + Home touch button; $99, deliveries from 2026-08-02.
  source: https://www.cnx-software.com/2026/07/23/99-xteink-x4-pro-4-3-inch-touchscreen-ereader-to-support-crosspoint-reader-open-source-firmware/
  publisher: CNX Software
  pub_date: 2026-07-23
  accessed: 2026-09-26
  confidence: medium (MCU/PSRAM unconfirmed by vendor; resolution and touch controller not stated)
  class: version
- claim: reTerminal Sticky: ESP32-S3R8 (dual LX7 240 MHz), 8 MB PSRAM, 32 MB flash, microSD, 3.97" 800x480 mono ePaper 4-level gray, capacitive touch, Wi-Fi 4 + BLE 5.0, 750 mAh USB-C, PDM mic, temp/humidity, 3-axis accelerometer, buzzer, 3 buttons (AI voice, prev, next), magnet mount, $50, shipping from 2026-09-15; supported by CrossPoint, TRMNL, ESPHome, OpenDisplay.
  source: https://www.cnx-software.com/2026/07/31/reterminal-sticky-3-97-inch-magnetic-touch-epaper-display-is-supported-by-four-open-source-firmware-projects/
  publisher: CNX Software
  pub_date: 2026-07-31
  accessed: 2026-09-26
  confidence: high
  class: version
- claim: Bluetooth is dropped to save RAM by every custom Xteink firmware except Stock, CrumBLE, SUMI and CrossPoint BLE (on C3 devices) — relevant to BLE-based device-to-device multiplayer on C3.
  source: https://pocketink.io/firmware/
  publisher: PocketInk
  pub_date: 2026-08-24
  accessed: 2026-09-26
  confidence: medium
  class: architecture
- claim: A new Xteink X4 Classic (X4 V2) launched Sept 2026 at $79; no CrossPoint release or fork supports it yet.
  source: https://pocketink.io/firmware/
  publisher: PocketInk
  pub_date: 2026-08-24 (page updated for Sept launch)
  accessed: 2026-09-26
  confidence: medium
  class: version
## Leads worth chasing
- CrossMux apps guide + its Seeed Sticky build (direct competitor on our target hardware).
- CrossInk Games Edition release notes (which games on X4 Pro, touch?).
- CrossPoint Flow Lua repo (ideo2004-afk/crosspoint-reader-lua 404 — find current URL).
- KOReader / reMarkable / Inkplate / M5Paper game ecosystems; Tatham puzzles ports to Kindle/Kobo/Rockbox.
- r/xteink user voice on games/multiplayer; X4 Pro reviews (GBAtemp, abstractnonsense) for touch latency/refresh.
## Looked for, not found
- Picoread repo (GitHub search "picoread" found nothing Xteink-related).
- Maintainer rationale on CrossPoint #678 closure.
- Vendor-official X4 Pro MCU/PSRAM/resolution/touch-controller spec.

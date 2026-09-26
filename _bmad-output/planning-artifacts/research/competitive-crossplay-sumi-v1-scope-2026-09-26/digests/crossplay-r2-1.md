# CrossPlay teardown — round 2
Note: GitHub API and GitHub MCP were denied for ma-r-s/crossplay in this session (403 "not enabled for this session"); repo metadata came from WebFetch of github.com HTML pages.

## Findings
- claim: Repo has 74 stars, 16 forks, 1 watcher, 7 open issues and 7 open PRs; the page reports 5,018 commits on `xteink`. No star history was available, so the trend is unknown.
  source: https://github.com/ma-r-s/crossplay
  publisher: GitHub
  pub_date: 2026-09-26 (live page)
  accessed: 2026-09-26
  confidence: medium (WebFetch summarizer; counts not cross-checked)
  class: trajectory
- claim: Releases ship very often and are bot-cut. The clone holds 78 tags, and minor versions went 1.7.0 (2026-08-28), 1.10.0 (08-30), 1.12.0 (08-31) and 1.13.0 (09-12). Nine patch releases (1.13.9 to 1.13.18) shipped on 2026-09-21 to 22 alone. The release bot also rebuilds the WASM emulator on each merge.
  source: https://github.com/ma-r-s/crossplay/releases ; git tag/log in clone @31d94db1; docs/release-notes.md@31d94db1
  publisher: ma-r-s / crossplay-release[bot]
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: version
- claim: Recent work has shifted away from games toward "Live", a phone-to-device picture and drawing app with a cloud service (fridge-bridge, Vercel). All v1.13.16 to 1.13.18 notes are about Live or wallpapers. The last link/multiplayer-layer commits in the shallow history are dated 2026-09-04.
  source: https://github.com/ma-r-s/crossplay/releases ; git log -- src/apps_local/link @31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: medium (depth-200 history)
  class: trajectory
- claim: User voice is thin: 12 issues in total (7 open, 5 closed), all dated 2026-08-31 to 2026-09-24. None is about Play Nearby or multiplayer bugs. The only game request is #202 (2026-09-13), which asks for word-generator databases for party games (Time's Up, Blank Slate, Herd Mentality, Poetry for Neanderthals) "even if its not feasible to have the games themselves fully coded". It has no maintainer reply. The other issues are reader or feature asks: interactive-EPUB gamebook links (#262), clickable notes (#175), a QR generator (#174), slow page turns (#7), and failing updates and Get Books (#191, #203). #231 is titled "Works well".
  source: https://github.com/ma-r-s/crossplay/issues?q=is%3Aissue ; https://github.com/ma-r-s/crossplay/issues/202
  publisher: GitHub users
  pub_date: 2026-08-31..2026-09-24
  accessed: 2026-09-26
  confidence: medium
  class: sentiment
- claim: The maintainer's own board records that as of 2026-09-06, 1 of 378 cards came from a user who is not Mario. 75 came from Mario and 300 were "session"-found, meaning found by LLM agents in audits, gates and cold reviews. The roadmap is driven by the maintainer and his agents, not by the community.
  source: docs/workflow/what-mario-reported.md@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-06
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: No Reddit or forum discussion of CrossPlay was found by web search. Search surfaced only GitHub forks (e.g. jvfajardz, terraflubb, TheChrisVela).
  source: WebSearch "CrossPlay xteink firmware games reddit"; "xteink games firmware multiplayer site:reddit.com"
  publisher: n/a
  pub_date: 2026-09-26
  accessed: 2026-09-26
  confidence: low (search coverage)
  class: sentiment
- claim: ESP-NOW limits relevant to going beyond 2 players: at most 20 peers in the peer list (encrypted up to 17, default 7). Payloads are 250 B in v1 and 1470 B in v2. Broadcast is supported but cannot be encrypted. Sleep works only in station mode, with esp_now_set_wake_window controlling the RX window. So the radio does not stop a 3 to 8 player star or mesh topology; the limit is CrossPlay's session design.
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif
  pub_date: unknown (stable docs)
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: CrossPlay's link has no encryption or authentication. Both broadcast and unicast peers are added with encrypt=false, and a unicast peer is auto-added on first send. A full peer table fails the send and is retried. Anyone on channel 1 running the same build and GameId could inject States. The protocol assumes a cooperative room.
  source: src/apps_local/link/LinkRadio.cpp@31d94db1 L217-235, L287-300
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: The two-seat assumption reaches into the UI as well as the session. LinkScreens draws "two seats and the link between them", and Hearts' design doc explicitly declines link play because "the radio seats two". Going beyond 2 players is a redesign of the session, the screens and turn-taking, not a constant change.
  source: src/apps_local/link/LinkScreens.h@31d94db1 L15-21; docs/apps/hearts.md@31d94db1 L7
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture

## Leads worth chasing
- Star-history trend (star-history.com or GitHub API with credentials).
- Whether forks (16) carry divergent game work.
- docs/apps/guesswho.md "One device, two secrets": a design-only nearby game that uses player avatars as characters.
- Battery cost of a Play Nearby match (the code comment says "UNMEASURED").

## Looked for, not found
- Reddit, r/eink or other forum threads about CrossPlay.
- GitHub Discussions (not seen on the repo page).
- Any issue reporting a Play Nearby failure or asking for more than 2 players or pass-and-play.
- A star trend over time.

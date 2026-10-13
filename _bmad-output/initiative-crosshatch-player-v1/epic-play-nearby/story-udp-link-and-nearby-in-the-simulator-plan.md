---
title: 'UDP link and nearby in the simulator'
type: 'feature'
ticket: '16'
created: '2026-10-13'
baseline_revision: 'a30727191db8a17d4840016ea650476eb0ac80a8'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/hal-and-abstractions/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.agents/skills/scope-discipline/SKILL.md'
warnings: ['oversized']
deferred:
  - summary: >-
      `sim_instance_test.py` lives in the skill folder, so CI's `scripts/*_test.py` glob never runs it.
    evidence: |-
      `scripts/sim_sh_test.py` and `crosshatch-ci.yml` are outside this entry's `touches`. Move the cases into `scripts/` (or a new `scripts/sim_instance_test.py`) when a touches list allows; it runs in seconds.
    location: >-
      .claude/skills/run-crosshatch-player/sim_instance_test.py
    severity: low
  - summary: >-
      `sim.sh start`'s per-instance environment (`CROSSPOINT_SIM_SD`, `CROSSPOINT_SIM_HTTP_PORT`, `CROSSHATCH_UDP_PORT_BASE`) is observed only by the manual two-instance run, not by a test.
    evidence: |-
      A test would need stub `Xvfb` and binary that dump their environment. The two-instance run of this entry shows the SD roots, ports, and base are honoured. Severity if a variable were dropped: medium (instances collide or never meet).
    location: >-
      .claude/skills/run-crosshatch-player/sim.sh:cmd_start
    severity: low
  - summary: >-
      The simulator branches of `NearbyRadio::open()` (link and label copy) and `NearbyProbe::read()` are compiled and run only by the simulator, not by a host suite.
    evidence: |-
      The host build does not define `SIMULATOR` and builds neither `GameLobbyActivity.cpp` nor `NearbyProbe.cpp` with it. Covering them means a `SIMULATOR` build of the lobby activity in the harness. The two-instance run shows the host label ("Simulator <port>") on the host screen and the guest's list, which exercises the label copy.
    location: >-
      src/activities/games/GameLobbyActivity.cpp:NearbyRadio::open; src/games/NearbyProbe.cpp:read
    severity: low
  - summary: >-
      `test/game_core/LinkContract.h` still has no three-link variant; `ThreeLinkContract.h` in the harness folder covers `UdpLink` and `FakeLink` only.
    evidence: |-
      `LinkContract.h` is outside this entry's `touches`. `deferred-work.md` `## 6.2`'s second item is met for `UdpLink` here; `EspNowLink` can run the variant only in a device run (entry 11).
    location: >-
      test/game_script/harness/ThreeLinkContract.h
    severity: low
---

<intent-contract>

## Intent

**Problem:** Nearby can only be tried on two X4 Pros (entry 11), and the lobby, the round, and the "player left" view have no simulator screenshots. Under `SIMULATOR` the host caps report nearby off and the lobby's radio starts no link.

**Approach:** A third `GameCore::ILink`, `UdpLink`, in `src/games`, compiled only under `SIMULATOR`: it binds the first free port of an 8-port range on localhost (the base moved by an environment variable), broadcasts as a send to every other port, uses the port (with an instance tag) as the peer handle, and carries the host label. Under `SIMULATOR` the host caps report nearby on, the probe reads Wi-Fi off and the simulator's heap, and `NearbyRadio::open()` hands the lobby a `UdpLink`. `sim.sh` gains a numbered instance (own display, state directory, SD root), so two simulators play one round. The device and the host suites other than the new one are unchanged.

## Boundaries & Constraints

**Always:** `UdpLink.cpp` is one whole-file `#if FREEINK_CAP_GAMES && defined(SIMULATOR)`; its header names no socket type and includes only `lib/GameCore`, `lib/Logging`, and the standard library. The port base is read from the environment each time a link starts (a function call, never a static initializer). A received datagram longer than the caller's buffer, or from a port outside the range, is dropped and never truncated. Allocation is `new (std::nothrow)` with a null check. No function local over 256 B. The device ELF (x4pro, default) is unchanged in size.

**Never:** Edit `lib/GameCore/**`, `src/games/EspNowLink.*`, any upstream file, `platformio.ini`, `games/**`, `test/game_script/first_party/**`, or an existing file under `test/`. Add `begin()` or any socket type to `ILink`. Put a socket header in a Screens file. Change `scripts/sim_sh_test.py` (outside `touches`; see Design Notes 7).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Two links | `create(base)` twice | ports `base`, `base+1`; each hears the other's broadcast; `expectLinkContract` passes | none |
| Three or eight links | up to 8 `create(base)` | distinct ports and tags; a listener names 7 senders with 7 distinct valid handles; all 8 pairwise-distinct per link | the 9th `create` returns null, `LOG_ERR`; after one `end()` a new `create` takes that port |
| Oversize datagram | raw socket inside the range sends 2,000 B, or a frame longer than the caller's buffer | not delivered, no truncated copy; the next datagram is delivered | none |
| Stranger | datagram from a port outside the range, or from the link's own port | dropped | none |
| Foreign or dead handle | `send` to `NO_PEER`, another link's handle, an unheard port, or after `end()` | false, nothing sent | none |
| Base from the environment | unset / `"abc"` / `"1000"` / `"65530"` / `"46000"` | 45700 / 45700 / 45700 / 45700 / 46000; read afresh at each start | out-of-range value ignored, as crossplay |
| Lobby under `SIMULATOR` | New game, Nearby, Host or Join | the lobby starts a `UdpLink`, label `Simulator <port>` | no free port: `open()` returns no link, the lobby shows "Nearby could not start" |
| Two simulators | `SIM_INSTANCE=1` and `=2`, same worktree | each: own display, state dir, SD root and inbox; same UDP base; they find each other | `SIM_INSTANCE` not `1`..`9`: `sim.sh` exits 1 naming it |

</intent-contract>

## Code Map

- `lib/GameCore/ILink.h`, `lib/GameCore/Protocol.h` (`MAX_FRAME_BYTES` 1470, `MAX_HOST_LABEL_BYTES` 16) -- the interface and limits `UdpLink` implements; read-only.
- `src/games/EspNowLink.h` -- the model: `PeerTable` (handle = tag << 8 | slot + 1), `takeInstanceTag`, `constinit` atomic tag, the MIT notice crossplay's code carries, `hostLabel()`; do not include it or edit it.
- `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/crossplay-src/crossplay/src/apps_local/link/LinkRadio.cpp` lines 33-91 and 338-437 -- the simulator branch to adapt (`basePort`, `addressForPort`, bind loop, broadcast as a send to every other slot, `receive`); read-only; name it in the new file's header with the MIT notice.
- `src/games/NearbyProbe.{h,cpp}` -- `read()` is device-only (`#if FREEINK_CAP_GAMES && !defined(SIMULATOR)`); `Readings{wifiInUse, freeInternalHeapBytes}`.
- `src/games/GameHostCaps.{h,cpp}` -- `NEARBY = NEARBY_BUILT && !IS_SIMULATOR`; `HostCapsValues::NEARBY_BUILT` stays false (the host double and `GameHostCapsTest` read it).
- `src/activities/games/GameLobbyActivity.{h,cpp}` -- `NearbyRadio::probe()`/`open()` (lines ~55-72) are the device wiring; `Opened{link, label[16], labelLength}`; the lobby calls `radio->open()` in `openHost`/`openGuest`.
- `src/games/GameClock.cpp`, `GameRandom.cpp` -- already `SIMULATOR`-aware; unchanged.
- `scripts/check_layers.py`, `scripts/check_layers_test.py` -- `RADIO_CRYPTO_PATTERNS`, `PLATFORM_PATTERNS`, `classify_external`, `BEYOND_TABLE`, `games_branch`/`games_guarded_lines`, `check()`; the test's `BASE` tree and `assert_fails`.
- `.claude/skills/run-crosshatch-player/sim.sh`, `SKILL.md`, `simulator.ini` -- the driver (`STATE`, `SHOTS`, `DISP`, `cmd_start`, `cmd_stop`, `cmd_ss`); the simulator library reads `CROSSPOINT_SIM_SD` (SD root, default `./fs_`) and `CROSSPOINT_SIM_HTTP_PORT` (default 8080; the socket server uses it and +1).
- `scripts/sim_sh_test.py` -- copies only `sim.sh` and `simulator.ini` into a scratch repo, so `sim.sh` must stay one file and `setup`/`check` unchanged; read-only here.
- `test/game_core/LinkContract.h` (`expectLinkContract(a, b, pump)`), `test/game_core/FakeLink.h` (`FakeLink`, `FakeLinkNet`, `ManualClock`), `test/game_core/FakeLinkTest.cpp` (`TwoPeersGetTwoHandlesAndNeverShareOne`, `AForeignHandleReturnsFalseAndIsCounted`) -- read-only; reuse.
- `test/game_script/harness/nearby_session.cmake`, `nearby_session.sources.cmake`, `esp_now_link.cmake`, `NearbySessionSupport.h` (`Duo`, `ObservedLink`, `waitFor`, `FakeRtosClock`), `stubs/Logging.h` -- how a suite is added without editing `CMakeLists.txt` (`<name>.sources.cmake` runs first, `<name>.cmake` is a suite); `game_match_src` holds `NearbySession.cpp`/`GameLink.cpp`.
- `test/game_script/fixtures/nearby/` (`manifest.json`, `main.lua`) -- the round the simulators play; `scripts/pack_game.py <dir> <out_dir>` packs it.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/UdpLink.h`, `src/games/UdpLink.cpp` -- `class UdpLink final : public GameCore::ILink`, static `SLOTS = 8`, `DEFAULT_BASE_PORT = 45700`, `BASE_PORT_ENV = "CROSSHATCH_UDP_PORT_BASE"`, `static uint16_t basePortFromEnvironment()` (getenv per call; value in 1024..65535-SLOTS else default), `static std::unique_ptr<UdpLink> create(uint16_t basePort)` (null: no socket, non-blocking set failed, or all eight binds failed; `LOG_ERR("LINK", ...)`), `std::unique_ptr<UdpLink> makeUdpLink()` (create from the environment), `uint16_t port() const`, `std::span<const uint8_t> hostLabel() const` (`"Simulator <port>"`, 15 B). Handle = `(instanceTag << 16) | port`, tag 1..0xFFFF from a `constinit std::atomic<uint32_t>`; a heard-slot mask names a peer on first receipt (source port inside the range, not own); `send` needs a handle of this tag whose slot was heard; `broadcast` sendto every other slot (true when any took it); `receive` uses `recvmsg` with a `MSG_TRUNC` check, loops past dropped datagrams, returns false on `EAGAIN`; `end()` closes, clears the mask, idempotent; destructor ends. Whole-file guard `#if FREEINK_CAP_GAMES && defined(SIMULATOR)`; MIT notice and the crossplay source file named, as `EspNowLink.h` does.
- [ ] `src/games/NearbyProbe.cpp` -- add a `SIMULATOR` branch of `read()`: `Readings{false, HalMemory::getInternalHeap().freeBytes}`; the file is then `#if FREEINK_CAP_GAMES` with the Wi-Fi branch under `#if !defined(SIMULATOR)`; the comment in `NearbyProbe.h` that says "(device only)" is updated.
- [ ] `src/games/GameHostCaps.cpp`, `src/games/GameHostCaps.h` -- `NEARBY = HostCapsValues::NEARBY_BUILT || IS_SIMULATOR`; update both comments (the simulator now has nearby over `UdpLink`); `NEARBY_BUILT` stays false.
- [ ] `src/activities/games/GameLobbyActivity.cpp`, `GameLobbyActivity.h` -- `NearbyRadio::probe()` returns `NearbyProbe::read()` in every build (drop the `SIMULATOR` special case); `NearbyRadio::open()` under `SIMULATOR` calls `makeUdpLink()` and returns the link and the label bytes (`#include "games/UdpLink.h"` only under `SIMULATOR`), otherwise unchanged (`Opened{}`); update the comments naming entry 16.
- [ ] `scripts/check_layers.py` -- a `SIM_SOCKETS` target (`arpa/inet.h`, `netinet/in.h`, `sys/socket.h`, `sys/uio.h`, `fcntl.h`) matched before `PLATFORM_PATTERNS`, allowed only to `src/games` (in `BEYOND_TABLE`, not `TABLE`, which `SpineTest` compares with the spine), and an include of it must sit in a branch compiled only with `SIMULATOR` (`#if defined(SIMULATOR)`, `#ifdef SIMULATOR`, or such a conjunct, with no `||`, as the games guard is read); generalize `games_branch`/`games_guarded_lines` over the macro without changing their games behavior; update the module docstring.
- [ ] `scripts/check_layers_test.py` -- cases: the admitted include in `src/games/UdpLink.cpp` under `#if FREEINK_CAP_GAMES && defined(SIMULATOR)` passes; the same include unguarded, under `!defined(SIMULATOR)`, under `defined(SIMULATOR) || X`, in `src/activities/games`, in `lib/GameCore`, and in a `src/games` file under only `FREEINK_CAP_GAMES` each fails with the file and line; `unistd.h` stays admitted unguarded; the existing cases pass unchanged.
- [ ] `.claude/skills/run-crosshatch-player/sim.sh` -- `SIM_INSTANCE=N` (`1`..`9`; unset or empty: today's behavior, byte for byte): display `:(77+N)` unless `SIM_DISPLAY`, state `build/sim/inst-N` (pids, log, `shots/`), SD root `build/sim/inst-N/fs_` exported to the launched binary as an absolute `CROSSPOINT_SIM_SD`, `CROSSPOINT_SIM_HTTP_PORT=8080+10N`; `start` exports `CROSSHATCH_UDP_PORT_BASE` (the caller's, else derived from the repo root: `21000 + (cksum % 1000) * 8`, so worktrees running at once do not hear each other) and creates the SD root and `games/`; `where` prints the instance's `key=value` lines without side effects; `inbox DIR` packs a game folder with `scripts/pack_game.py` into the instance's `fs_/games/` (the default instance's is `fs_/games`); an invalid `SIM_INSTANCE` dies. `setup`, `check`, `build`, `script`, and every command without `SIM_INSTANCE` are unchanged. Everything stays in `sim.sh` (no helper file).
- [ ] `.claude/skills/run-crosshatch-player/SKILL.md` -- a section for two instances: the commands, the nearby recipe (pack the `nearby` fixture into each inbox, start both, drive the lobby), the UDP base variable, and the lobby landmarks found while taking the screenshots; the "nearby is off in the simulator" statements, if any, corrected.
- [ ] `.claude/skills/run-crosshatch-player/sim_instance_test.py` -- standard library, bash, awk, git only, in the manner of `scripts/sim_sh_test.py`: in a scratch git repo with `sim.sh` copied in, `where` for no instance, `1`, `2`, and `9`, an invalid `0`/`10`/`a`, an overridden base, two different roots giving two different bases in 21000..28999, no side effects (nothing created), and `setup`/`check` output unchanged with `SIM_INSTANCE` set.
- [ ] `test/game_script/harness/udp_link.sources.cmake`, `udp_link.cmake` -- exclude `UdpLink.cpp` from the shared libraries (it compiles empty there); a suite `UdpLinkHarnessTest` that builds `UdpLink.cpp` into its own library with `SIMULATOR=1 FREEINK_CAP_GAMES=1` and links `game_match_src`, `game_harness_core`, `GTest::gtest_main`, `Threads::Threads`.
- [ ] `test/game_script/harness/ThreeLinkContract.h` -- `expectThreeLinkContract(ILink& a, ILink& b, ILink& c, pump)`: the checks `LinkContract.h` cannot make with two links (deferred-work `## 6.2`, second item): each of the three hears the other two broadcast with valid, distinct handles; the six handles are pairwise distinct across links and stable on a second round; no `receive` returns `NO_PEER`; each link's handle for a peer is refused by the third link's `send`.
- [ ] `test/game_script/harness/UdpLinkTest.cpp` -- the matrix rows as `TEST`s: `expectLinkContract` over two `UdpLink`s and `expectThreeLinkContract` over three, with the pump a 2 ms sleep; the same `expectThreeLinkContract` over three `FakeLink`s (the double and the real link agree); 8 links, the 9th refused, a port freed by `end()` reusable; oversize datagram and stranger dropped via a raw loopback socket (a test tool, not a double); the base variable table above read afresh per `create`; `hostLabel()` form; every test picks a per-process free base (`pid`-derived, all eight ports probed free, retried) and sets the variable itself; one test runs `NearbySession::openHost`/`openGuest` over two `UdpLink`s on `Duo`'s rig (lobby handshake: listed, joined, started) and ships a `STATE` each way.

**Acceptance Criteria:**
- Given two simulator instances of one worktree, each with the `nearby` fixture in its own inbox, when one hosts and the other joins, then the host shows its label, "Player 2 joined", and Start, and the guest lists the host and shows "Waiting for the host to start".
- Given a started round, when each player taps a free square in turn and the guest taps a taken one, then the board follows on both, the rejected tap shows "That square is taken" on the guest only, and a won round shows Over on both with "waiting for host" for the guest.
- Given the host presses Play again and then Leave, then the round restarts on both, and the guest shows the "player left" view with its reason line.
- Given an x4pro or default firmware build, then its `firmware.bin` size and the x4pro games-on minus off flash and static-RAM difference equal the figures at `baseline_revision`, measured like for like.

## Implementation Notes

Implemented by one foreground implementation subagent from this plan (full route). Its hand-back report was not relayed to the orchestrator (the harness delivered it to this session as a message; it is summarized here). Files: `src/games/UdpLink.{h,cpp}` (new), `src/games/NearbyProbe.{h,cpp}`, `src/games/GameHostCaps.{h,cpp}`, `src/activities/games/GameLobbyActivity.{h,cpp}`, `scripts/check_layers.py` and `_test.py`, `.claude/skills/run-crosshatch-player/{sim.sh,SKILL.md,sim_instance_test.py}`, `test/game_script/harness/{udp_link.cmake,udp_link.sources.cmake,ThreeLinkContract.h,UdpLinkTest.cpp}`.

Decisions the plan left to the implementation: a base env value that is not a whole number (`46000x`) falls back to the default (stricter than crossplay's `atoi`); `create()` also refuses a base of 0 or above 65527; `sim.sh` derives and exports the UDP base only for a numbered instance (the default instance reads the caller's variable, else 45700); `sim.sh setup` with `SIM_INSTANCE` set creates the instance's `shots/` directory (the managed block bytes are unchanged, the test checks it); `script` with `SIM_INSTANCE` set uses the instance's state directory but the shared `./fs_`.

Not done by the implementer, by the plan's order: any `pio`/`sim.sh` build; `SKILL.md` has the recipe but no lobby tap coordinates (they come from the manual run, filled in then). No test exercises "a datagram from the link's own port" (a raw socket cannot hold that port; the code drops it).

Confirmed by the build session: `cmake --build` and `ctest` (2,518 tests pass, 2 dump-on-request tests skipped; `UdpLinkTest` 13 tests), every `scripts/*_test.py` and `sim_instance_test.py`, `scripts/check_layers.py` (834 edges), and `scripts/check_upstream_touches.py` PASS.

## Plan Change Log

## Review Triage Log

### 2026-10-13 - Review pass (first)
Lenses ran as four context-free foreground subagents launched in one message (blind hunter, edge-case hunter, verification gap, intent alignment) over `git diff HEAD` of the work tree (untracked files included, written through a scratch index; nothing staged).
- verdicts: 36 findings - high 0, medium 4, low 27, false 5, maybe-false 0
- findings:
  - (verification-gap) `[medium]` `[patch]` The simulator's `nearby = true` host cap is pinned by no test (the host build has no `SIMULATOR`) - confirmed (`GameHostCapsTest` reads `NEARBY_BUILT` only); fix: `GameHostCapsSimulatorTest` builds the file with `SIMULATOR=1` in its own executable. Same root as the blind hunter's "no test for gameHostCaps".
  - (verification-gap) `[low]` `[defer]` `NearbyRadio::open()`'s simulator branch is never run by the host suite - deferred (needs a `SIMULATOR` build of the lobby activity); the two-instance run covers it.
  - (verification-gap) `[low]` `[defer]` `NearbyProbe::read()`'s simulator branch has no test - deferred, same reason; two lines.
  - (verification-gap) `[low]` `[defer]` `sim.sh start`'s exported variables are unobserved by a test - deferred; `sim_instance_test.py` not in CI is a second claim of the same finding, deferred with it (CI wiring is outside `touches`).
  - (blind) `[false]` `[reject]` `CROSSPOINT_SIM_SD` and `CROSSPOINT_SIM_HTTP_PORT` are not read by anything - disproved: `.pio/libdeps/simulator_x4pro/simulator/src/HalStorage.cpp:21` reads `CROSSPOINT_SIM_SD`, `SimulatorNetworkPorts.h` reads `CROSSPOINT_SIM_HTTP_PORT`; the two-instance run confirms.
  - (blind) `[medium]` `[patch]` No test or doc for the `gameHostCaps()` change - same root as the first row; a grep of `docs/` and the spine finds no text left saying the simulator has no radio.
  - (blind) `[low]` `[patch]` Host-test port bases (21000..60992) overlap the ephemeral range and sim.sh's range - fixed: bases 11000..20992, the stranger on a fixed port (same root as the edge hunter's two test rows).
  - (blind) `[low]` `[patch]` `strtol` accepts " 46000" and "+46000" - fixed: first character must be a digit; rows added.
  - (blind) `[low]` `[patch]` `heard_` is tied to `SLOTS == 8` with no check - `static_assert` added.
  - (blind) `[low]` `[patch]` `receive()` does not enforce `MAX_FRAME_BYTES` (a test passes only through its buffer size) - fixed with a drop and a test with a 4,000 B buffer (same root as the edge hunter's first row).
  - (blind) `[low]` `[patch]` `check_layers.py`: conjunct picked by `macro == GAMES`, a 130-column docstring, no `#else`/`#elif` test for the SIMULATOR reader - dictionary keyed by macro, rewrapped, tests added (same root as the edge hunter's `games_branch` row).
  - (blind) `[low]` `[defer]` `sim_instance_test.py` is not run in CI - deferred with the verification-gap row.
  - (blind) `[low]` `[patch]` SKILL.md's lobby recipe has no coordinates - to be written from the manual run (this build session does it before the commit; not a defect of the implementation).
  - (blind) `[low]` `[reject]` Planning identifiers ("entry 16", "epic R8") in source comments - the same convention as `EspNowLink.h`, `GameLobbyActivity.h`, and `NearbySession.h` merged earlier; a rename touches code this entry does not change.
  - (blind) `[false]` `[reject]` MIT attribution only in the header - disproved: it is the pattern of `EspNowLink.h` (merged, reviewed), and the notice text and copyright line are copied from it.
  - (blind) `[false]` `[reject]` `GameLobbyActivity.cpp` leans on transitive includes and is "upstream-adjacent" - disproved: the file includes `<algorithm>`, `<span>`, `<utility>` itself and `<memory>` through its header, and `src/activities/games/` is a fork-only path.
  - (edge) `[low]` `[patch]` Oversize datagram delivered to a larger buffer - same root as the blind row above.
  - (edge) `[low]` `[reject]` A flood of dropped datagrams keeps `receive()` looping - the socket buffer bounds it, a localhost-only scenario with no hostile party, and a bound adds a branch.
  - (edge) `[low]` `[reject]` Non-EAGAIN `recvmsg` errors are not logged - unlikely on a bound loopback UDP socket; a log line adds a branch for no reachable case.
  - (edge) `[low]` `[reject]` `sendto` EINTR on a unicast send reports a lost send - `ReliableLink` resends; a retry loop is more than a direct correction for a rare signal.
  - (edge) `[low]` `[patch]` Stranger `RawSocket(0)` could land in the range - fixed with the range change.
  - (edge) `[low]` `[patch]` `freeBase()` overlaps the ephemeral range - fixed (same root as the blind row).
  - (edge) `[low]` `[patch]` An invalid `SIM_INSTANCE` makes every command exit 1 although SKILL.md says four commands ignore it - SKILL.md corrected (the strict behaviour is the safer one).
  - (edge) `[low]` `[patch]` `script` with a valid instance uses the instance's state but the shared `./fs_` - SKILL.md corrected, behaviour kept (`script` is the single-instance one-shot).
  - (edge) `[medium]` `[patch]` The default instance and a numbered one bind different UDP ranges and never meet - fixed: every `start` exports the base (verified by `where`; the test asserts they share it).
  - (edge) `[low]` `[reject]` `SIM_DISPLAY` set while starting two instances puts both on one display - a caller override that names the same display twice; the two-instance recipe does not set it.
  - (edge) `[low]` `[reject]` A stale Xvfb lock makes `start` wait 10 s - pre-existing behaviour of the default instance, not caused by this change.
  - (edge) `[low]` `[reject]` A hand-set invalid `CROSSHATCH_UDP_PORT_BASE` is printed by `where` while `UdpLink` falls back - both instances fall back to the same range and still meet; cosmetic.
  - (edge) `[false]` `[reject]` The label is 15 B - it is 15 B for the five-digit ports sim.sh derives (21000..28999) and shorter for four digits; the plan's "15 B" is the derived case.
  - (edge) `[low]` `[patch]` `games_branch` uses the SIMULATOR pattern for any other macro - same root as the blind `check_layers` row.
  - (edge) `[false]` `[reject]` Existing `sys/socket.h`, `sys/uio.h`, or `fcntl.h` includes in other game components start failing - `scripts/check_layers.py` passes on the tree (834 edges); none exist.
  - (edge) claims: `[low]` `[patch]` setup/check/build/script "unchanged by SIM_INSTANCE" (same as the SKILL.md rows); `[medium]` `[patch]` `start` exports the base only for numbered instances (same root as the default-instance row); `[low]` `[reject]` `receive` "returns false on EAGAIN" silently on other errors (same as the logging row); `[low]` `[patch]` SKILL.md landmarks not written (same as the blind row).
  - (intent) `[low]` `[reject]` Reading R-C (two live simulators play a round) is expected at the running simulator, and the diff's tests exercise the link, the layer rule, and `sim.sh where`; the lobby, caps, and probe changes have no host test - descriptive, not a defect: the two-instance run in Verification is the evidence for R-C, and the untested wiring is the verification-gap rows above (one patched, three deferred). The auditor's other notes (tagged handle, "device suites unchanged" read as additive, no flash measurement in the diff) are covered by Design Notes 1 and 10 and the Verification results.
- routes: no intent_gap or bad_plan entries; patches applied by the same implementation subagent, re-engaged; four deferred items in the frontmatter.

## Design Notes

1. **Handle and tag.** `ILink.h` (Rules) and the epic's Assumption (orchestrator, 2026-10-12, from entry 2's review) make handle values unique across link instances with an instance tag in the high bits; `UdpLink` takes the port in the low 16 bits and a tag (1..0xFFFF) above it, from its own `constinit` atomic (AD-2: a mutable static over 64 B or dynamically initialized is forbidden; this is 4 B and constant-initialized; it exists only under `SIMULATOR`). A peer is "named" when a datagram from a port in the range other than its own is received, tracked in a one-byte mask; `send` to a handle of this link's tag whose slot was never heard is false (ILink.h: "names no peer this link has received from"). A different process that later takes a left peer's port is, to UDP, the same address; ILink.h's "never reused for a different peer" cannot be tested with ports and is not claimed.
2. **Port base.** R16 and the entry: the base is an environment variable, read "when the link starts, never in a dynamically initialized static (AD-2)", so `basePortFromEnvironment()` calls `getenv` each time (crossplay's `basePort()` caches in a function-local static, which AD-2 forbids). The name `CROSSHATCH_UDP_PORT_BASE` and the default 45700 (crossplay's `kDefaultBasePort`) are this plan's choice; no source fixes either. An out-of-range override is ignored as crossplay does.
3. **Never truncated.** `ILink.h` `receive`: a longer frame is dropped, not cut. A plain `recvfrom` into the caller's buffer cuts silently, so `UdpLink` uses `recvmsg` and drops on `MSG_TRUNC` in `msg_flags` (portable to macOS, where `recvfrom` does not return the real length). Crossplay's `receive` reads into a scratch of its maximum size and compares, which is the same intent. A zero-length datagram is dropped too (a frame is 1..`MAX_FRAME_BYTES`).
4. **Lobby seam (D12).** `story-lobby-screens-and-the-nearby-start-plan.md` D12 and the entry's `touches` ("choosing the link under `SIMULATOR` only") put the choice in `NearbyRadio::open()`; the label, probe, and clock are the same class's. The clock needs no change (`GameClock` is `SIMULATOR`-aware); `random()` is unchanged. The simulator code is kept to `makeUdpLink()` plus a label copy so the host suite, which cannot build `GameLobbyActivity.cpp` with `SIMULATOR`, tests everything but that copy; the simulator run shows the label on the host screen. `NearbyLobbyHarnessTest.TheFakeRadioHandsOutWhatTheDeviceRadioDoes` pins that the device's `open()` returns none; with `SIMULATOR` undefined in the host build it still does.
5. **Guards in what changes.** `gameHostCaps()` (git history: `3d4aec97`, `353bafd0`, `17d02483`) has no early return or ordering guard; `NEARBY` stays a `constexpr` so `check_flash_budget.py objects` sees no initializer, and the device value stays `false || false`. `NearbyRadio::probe()` had one guard, the `SIMULATOR` return `{false, SIZE_MAX}` because `NearbyProbe.cpp` was device only; the new reader gives Wi-Fi off (UDP on loopback does not touch Wi-Fi, so the simulator's web server may run) and the simulator heap (`CROSSPOINT_SIM_FREE_HEAP` exercises the low-memory refusal). `NearbyRadio::open()` on the device still returns `Opened{}` until entry 11.
6. **check_layers.** The entry's `verify`: "any socket header it needs is admitted for `src/games` under `SIMULATOR` only". A new target keeps the sockets out of `PLATFORM` (which Screens may include) and out of `TABLE` (the spine's row, which `SpineTest` compares; the orchestrator owns the spine, so the edge lives in `BEYOND_TABLE` with its reason). "Under `SIMULATOR` only" is enforced by the same guard reader the games flag uses, so an unguarded socket include in `src/games` fails. `unistd.h` (`close`) is already `PLATFORM`.
7. **sim.sh and its test.** The entry's unknown: a second instance needs its own SD root (`CROSSPOINT_SIM_SD`, read by the simulator's `HalStorage.cpp`), HTTP port, display, and state directory; the UDP base is shared, so it is the one thing both instances must agree on, and sim.sh derives it from the repo root so lanes running at once do not hear each other. `scripts/sim_sh_test.py` is outside `touches`, so the new test lives in the skill folder and CI's `scripts/*_test.py` glob does not run it: recorded as a deferred item (move or merge into `scripts/` when a touches list allows). `sim.sh` stays one file because `sim_sh_test.py` copies only it and `simulator.ini`.
8. **Three-link contract.** `deferred-work.md` `## 6.2` item 2 names this entry as the first user. `test/game_core/LinkContract.h` is outside `touches`, so the variant is a new header in the harness folder, run over `UdpLink` and over `FakeLink` (the double stands in for the link's contract on both sides and a test pins that they agree). `EspNowLink` still cannot run it on the host (device run, entry 11).
9. **Test doubles.** None added. The raw loopback socket in the tests is a test tool that stands for another process on the port range. `Duo`-style rigs reuse `FakeRtosClock` with real sockets: a bound in fake milliseconds is shorter in real time than on a device, as `NearbySessionSupport.h` says; no host timing bound is asserted.
10. **Flash.** `UdpLink.cpp` compiles empty off `SIMULATOR`; `NearbyProbe.cpp` and `GameHostCaps.cpp` change only under or around `SIMULATOR`; `GameLobbyActivity.cpp`'s device path is as before. Line numbers shift, so a `__LINE__` string could move a few bytes; the like-for-like measurement decides.

## Verification

**Commands:**
- `flock /tmp/crosshatch-build.lock flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, `UdpLinkHarnessTest` included (run it with `--repeat until-fail:20` for the sockets)
- `python3 scripts/check_layers_test.py`, `python3 scripts/check_layers.py`, `python3 .claude/skills/run-crosshatch-player/sim_instance_test.py`, every `python3 scripts/*_test.py` -- expected: pass
- `python3 scripts/check_upstream_touches.py` -- expected: pass
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing
- after the review, under `/tmp/crosshatch-build.lock`: `sim.sh build x4pro`, `sim.sh build sticky`, `pio run -e default`, `check_flash_budget.py build on|off|compare` and `pio run -e default`'s size, each at the base and at the commit's tree, like for like; `pio check` for `default` and `-e x4pro` as AGENTS.md says -- expected: build, and `firmware.bin` bytes and the games-on minus off difference equal at both commits

**Manual checks (simulator, two instances, `nearby` fixture in each inbox):**
- Lobby: Choose (both), Host waiting, Browse with the host, Joined, Host "Player 2 joined", Host "Player 2 left" and back, the guest's "The host left"; the greyed "Different version" row from a third instance holding the fixture at another version; no "That game is full" (two instances cannot reach it).
- Match: the first move on both, a rejected tap on the guest, a won round (Over on both), Play again, Leave on the host, "player left" on the guest. Every screenshot looked at before it is kept, under `story-udp-link-screenshots/`.

**Results (2026-10-13):**
- Host side, after the patches: `cmake --build build/test` and `ctest` 2,521 tests pass (2 dump-on-request tests skipped); `UdpLinkTest` (14 tests) and `GameHostCapsSimulatorTest` (2) 20 times in a row (`--repeat until-fail:20`) pass; every `scripts/*_test.py` and `sim_instance_test.py` pass; `scripts/check_layers.py` passes (834 edges); `scripts/check_upstream_touches.py` PASS (no upstream file touched); `./bin/clang-format-fix` twice, the second run changed nothing and it reformatted no file outside this story's paths.
- Firmware, like for like, under `/tmp/crosshatch-build.lock`, incremental tree, shared cache, the same worktree and path at both points: at the base `a30727191db8a17d4840016ea650476eb0ac80a8` (HEAD before any edit) and at the work tree that became the commit (it differs from the commit only in `SKILL.md` and this plan, edited after the builds). x4pro `firmware.bin` games on **6,244,176 B** at both; games off 5,957,328 B at both; difference **+286,848 B** at both (`compare` exit 0, 20,352 B under the 307,200 B gate); static internal RAM **+784 B** at both (190,248 B on, 189,464 B off; 240 B under 1,024 B); `objects` and `touch-line` exit 0. `default` `firmware.bin` 5,927,792 B at both and **byte-identical** (`cmp`; SHA-256 `f5f4a545...9864`); the x4pro games-on binaries are the same size and differ in bytes (the build's own embedded data), so the size and the games-on minus off difference are unchanged: **+0 B flash, +0 B static RAM** over the base, like for like. (The epic's projection stands at +28,800 B / +0 B reachable today, +40,256 B / +88 B once entry 11 makes the driver reachable.)
- `sim.sh build x4pro` and `build sticky` succeed (the simulator binary holds the `UdpLink` label format). `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` exits 0 for `default` and for `-e x4pro`.
- Two simulator instances (x4pro, `SIM_INSTANCE=1` and `2`, each with the `nearby` fixture packed by `pack_game.py` into its own inbox; a third, with the fixture at version 1.0.1, for the greyed row), driven with `sim.sh`: Games lists "Nearby - Pass and play - Play nearby" (caps on), Options cycles the mode to Play nearby, New game opens the lobby (probe passes), Host a game starts `UdpLink` ("udp link up on 23872"), the guest lists the host, joins, the host shows "Player 2 joined", the guest cancels and the host shows "Player 2 left", the guest rejoins and the host starts; the round: host's move drawn on the guest, a tap on the taken square on the guest shows "That square is taken" on the guest only, a won round shows Game over on both ("Waiting for the host" and Leave on the guest), Play again restarts it on both, Leave on the host brings up "The other player left" on the guest (log: `abort recv reason=left`, `Playing -> PeerGone`); a host that cancels shows the guest "The host left". Every screenshot was looked at before it was kept. Not captured: "That game is full" (two instances cannot reach it; entry 10's harness covers it) and the guest's "Different version" ended screen (a joinable listing never meets a version mismatch; the greyed list row is shown). The ESP-NOW path is untouched; timing figures are unmeasured (real loopback, no estimates asserted).
- Screenshots, in `story-udp-link-screenshots/`:
  - `lobby-choose.png` - Choose: Host a game, Join a game.
  - `lobby-host-waiting.png` - host: label "Simulator 23872", "Waiting for a player", "1 of 2 players", Start, Cancel.
  - `lobby-guest-browse-host-listed.png` - guest: the host listed with "1 of 2 players", Cancel.
  - `lobby-guest-browse-different-version.png` - guest: a host on another package version greyed with "Different version".
  - `lobby-guest-joined-waiting.png` - guest: "Waiting for the host to start" and the host's label.
  - `lobby-host-player2-joined.png` - host: "Player 2 joined", "2 of 2 players", Start enabled.
  - `lobby-host-player2-left.png` - host after the guest cancelled: "Player 2 left", waiting again.
  - `lobby-host-player2-rejoined.png` - host after the guest joined again: "Player 2 joined".
  - `lobby-guest-host-left.png` - guest after the host cancelled: "The host left", Back to the list.
  - `round-host-start.png` - host's board, "Player 1 (X) to move".
  - `round-guest-start.png` - guest's board, "Player 2 (O): Player 1 to move".
  - `round-guest-after-host-move.png` - guest's board showing the host's X, "Player 2 (O) to move".
  - `round-guest-rejected-move.png` - guest after tapping a taken square: "That square is taken".
  - `round-host-no-reject-message.png` - host at the same moment: no message (a rejected move stays on its device).
  - `round-host-over-play-again.png` - host: Game over, Play again and Leave.
  - `round-guest-over-waiting-for-host.png` - guest: Game over, "Waiting for the host", Leave.
  - `round-guest-after-play-again.png` - guest's empty board after the host's Play again.
  - `player-left-view.png` - guest after the host left: "The other player left", Back.

## Auto Run Result

**Summary:** Under `SIMULATOR`, nearby now runs over `UdpLink`, a third `ILink` (UDP on loopback, the first free port of eight, the port plus an instance tag as the peer handle, `Simulator <port>` as the host label, nothing truncated, the base read from `CROSSHATCH_UDP_PORT_BASE` at each start). The host caps report nearby on, the probe reads Wi-Fi off and the simulator's heap, and the lobby's radio opens a `UdpLink`. `sim.sh` runs numbered instances (`SIM_INSTANCE=1..9`: own display, state directory, SD root, inbox, web port; one UDP base per worktree), and two instances played a lobby and a round end to end. The device ELFs are unchanged in size (the default build is byte-identical).

**Files:**
- `src/games/UdpLink.{h,cpp}` - the link, whole-file `#if FREEINK_CAP_GAMES && defined(SIMULATOR)`.
- `src/games/NearbyProbe.{h,cpp}` - a simulator reader (Wi-Fi off, simulator heap).
- `src/games/GameHostCaps.{h,cpp}` - `NEARBY = NEARBY_BUILT || IS_SIMULATOR`, comments.
- `src/activities/games/GameLobbyActivity.{h,cpp}` - `probe()` reads `NearbyProbe::read()` everywhere; `open()` hands out a `UdpLink` and its label under `SIMULATOR`.
- `scripts/check_layers.py`, `scripts/check_layers_test.py` - `SIM_SOCKETS` headers admitted for `src/games` inside a `SIMULATOR` branch only, with tests.
- `.claude/skills/run-crosshatch-player/{sim.sh,SKILL.md,sim_instance_test.py}` - numbered instances, `where`, `inbox`, the recipe with the lobby landmarks, and the instance test (run by hand: CI's glob does not reach it).
- `test/game_script/harness/{udp_link.cmake,udp_link.sources.cmake,UdpLinkTest.cpp,ThreeLinkContract.h,game_host_caps_simulator.cmake,GameHostCapsSimulatorTest.cpp}` - the `UdpLink` suite over real loopback, the three-link contract (also over `FakeLink`), and the `SIMULATOR` host-caps suite.
- `story-udp-link-screenshots/` (18 PNGs) and this plan.

**Review breakdown:** four lenses ran (blind hunter, edge-case hunter, verification gap, intent alignment). 36 rows: 18 patched (grouped into 7 fixes: the `SIMULATOR` host-caps suite, test port ranges, the base-value parser, the `heard_` width check, the receive size bound, `check_layers` keyed guards and tests, `sim.sh`'s default-instance base and SKILL.md's claims), 4 deferred (see frontmatter), 14 rejected with reasons in the Review Triage Log (five false, the rest low and not worth the added branches, or pre-existing). Patched by severity: medium 2 entries (the host-caps pin, the default instance not meeting numbered ones), low 5 entries.

**Verification:** see Results above. Screenshots: 18 in `story-udp-link-screenshots/`, listed above.

**Formatting:** `./bin/clang-format-fix` changed only this story's own files (the implementer's run); no file outside its paths and no upstream file.

**Follow-up review recommended:** true. The patches after the review (the receive size bound in `UdpLink.cpp`, `check_layers.py`'s keyed guard patterns, `sim.sh`'s base export for every `start`) were verified by the host suites, the layer check, the `sim_instance_test.py` cases, the firmware measurements, and the two-instance run, but no second review lens read them; the unverified risk is a regression in those three changed paths that the tests above do not pin (for example a `sim.sh` command that differs only without `SIM_INSTANCE`).

**Residual risks:** `sim_instance_test.py` is not run by CI. The simulator branches of `NearbyRadio::open()` and `NearbyProbe::read()` are exercised only by the simulator run. No test pins "a datagram from the link's own port is dropped". The base derivation (`21000 + cksum % 1000 * 8`) can collide with another program's ports; `where` shows it and the caller's variable overrides it. A timing figure in this plan: none claimed.

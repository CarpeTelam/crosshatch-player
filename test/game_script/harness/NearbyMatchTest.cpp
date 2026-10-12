#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "GameHash.h"
#include "MatchSupport.h"
#include "NearbySessionSupport.h"
#include "SaveSupport.h"
#include "activities/games/GameMatchActivity.h"

// Two GameMatchActivity instances over FakeLink and the nearby fixture (entry 9 of epic-play-nearby): the mover
// applies, the STATE is shipped and adopted between the VM's rounds calls, the follower waits for the host, Play again
// is the host's, and nothing is saved. The real activities, GameVM, NearbySession, GameLink, and GameCore, over the
// screen doubles and a FakeLinkNet (NearbySessionSupport.h's Duo).
//
// Test doubles (orchestrated-epics.md, Build-agent brief):
// - The two "devices" share one fake card (fakesd), one log (fakelog), one clock (fakertos), one process, and one
//   ActivityManager double. They stand in for two devices that each have their own SD card, log, clock, and manager;
//   they are told apart by the game id each runs (nearby-a on the host, nearby-b on the guest:
//   match::installFixtureAs), so the ch.log lines (the id is their origin) and /.games-data/<id>/ are per device. A
//   test that reads a path outside those two ids would read the shared card. The shared manager counts both devices'
//   update requests: the test thread runs one device's loop() at a time and credits the asks made during it to that
//   device (Device::asked), which is exact because nothing else asks. More permissive than two devices: the loop is the
//   test thread alternating frame() on each device, not two cores; the render task is the test thread rendering between
//   loop passes; one core's worth of time per poll. MatchTest.TheTwoDevicesShareNoGameFiles pins that the two ids are
//   separate folders with the same game.
// - FakeLink and ObservedLink are NearbySessionSupport.h's (their headers list what they model).
// - nearby::waitFor moves the fake clock 2 ms per real millisecond of polling, so the sessions' tasks (which wait on
//   fake time) and the matches' timers make progress while the test waits; match::waitFor does not.

namespace {

using Button = MappedInputManager::Button;
using harness::Bytes;
using nearby::Duo;

constexpr int CANVAS_X = 3;
constexpr int CANVAS_Y = 6;
constexpr const char* HOST_ID = "nearby-a";   // seat 1
constexpr const char* GUEST_ID = "nearby-b";  // seat 2

// What a device is made of: its panel, its input, its activity, and the session the activity owns (kept to read the
// counters; valid while the activity lives).
struct Device {
  std::string id;
  uint8_t seat = 0;
  std::unique_ptr<GfxRenderer> renderer;
  std::unique_ptr<MappedInputManager> input;
  std::unique_ptr<GameMatchActivity> activity;
  NearbySession* session = nullptr;
  bool asked = false;   // the loop asked for a render since the last one
  bool exited = false;  // onExit has run
  bool loops = true;    // the test runs this device's loop (false: an overlay holds it)
};

class NearbyMatchTest : public match::ScreenTest {
 protected:
  void TearDown() override {
    fakertos::release();
    // What the manager does when the screen goes: onExit under the lock, then the destructor under it.
    for (std::unique_ptr<Device>* slot : {&guest, &host}) {
      if (!*slot || !(*slot)->activity) continue;
      if (!(*slot)->exited) activityManager.exitHolding(*(*slot)->activity);
      activityManager.destroyHolding((*slot)->activity);
    }
    host.reset();
    guest.reset();
    duo.reset();
    ScreenTest::TearDown();
  }

  // ---- bringing the two devices up ----

  // The pair of sessions, joined and started: the host holds seat 1.
  void startDuo() {
    duo = std::make_unique<Duo>();
    duo->openBoth();
    ASSERT_TRUE(duo->joinAndStart());
  }
  // Installs the nearby fixture for each device, with a .pkg when `withPkg` (the match only touches resume.bin for an
  // installed package).
  void installBoth(const bool withPkg = false) {
    for (const char* id : {HOST_ID, GUEST_ID}) {
      match::installFixtureAs("nearby", id);
      if (!withPkg) continue;
      char bytes[GamePkg::FILE_BYTES];
      GamePkg::formatPkg(savefile::HASH_A, bytes);
      fakesd::addFile(std::string("/.games/") + id + "/.pkg", std::string(bytes, sizeof(bytes)));
    }
  }
  GameCore::Manifest manifestFor(const std::string& id) const {
    GameCore::Manifest manifest = match::manifestOf(id, "Nearby");
    manifest.seatsMin = 2;
    manifest.seatsMax = 2;
    manifest.modes = GameCore::Manifest::MODE_PASS | GameCore::Manifest::MODE_NEARBY;
    manifest.settingsCount = declaredSettings;
    return manifest;
  }
  // Enters a device on `session`: the caller's roster and start are what a caller that knew nothing of the session
  // would pass (the session's roster wins).
  std::unique_ptr<Device> enter(const std::string& id, const uint8_t seat, std::unique_ptr<NearbySession> session,
                                const GameMatchActivity::Start start = GameMatchActivity::Start::New,
                                const GameCore::SettingValues& settings = {}) {
    auto device = std::make_unique<Device>();
    device->id = id;
    device->seat = seat;
    device->renderer = std::make_unique<GfxRenderer>(480, 800);
    match::addFonts(*device->renderer);
    device->input = std::make_unique<MappedInputManager>(gpio, *device->renderer);
    device->session = session.get();
    const int before = asks();
    device->activity =
        std::make_unique<GameMatchActivity>(*device->renderer, *device->input, manifestFor(id),
                                            GameCore::Roster::solo(), start, settings, std::move(session));
    device->activity->onEnter();
    device->asked = asks() > before;
    return device;
  }
  void enterHost(const GameMatchActivity::Start start = GameMatchActivity::Start::New,
                 const GameCore::SettingValues& settings = {}) {
    host = enter(HOST_ID, 1, std::move(duo->host), start, settings);
  }
  void enterGuest(const GameMatchActivity::Start start = GameMatchActivity::Start::New) {
    guest = enter(GUEST_ID, 2, std::move(duo->guest), start);
  }
  // Both devices, the host first, and the round's first frames on both panels.
  void startMatch(const bool withPkg = false) {
    installBoth(withPkg);
    ASSERT_NO_FATAL_FAILURE(startDuo());
    enterHost();
    enterGuest();
    ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }))
        << "the first round did not reach both panels";
  }

  // ---- the loop ----

  static int asks() { return activityManager.asks.updates + activityManager.asks.immediateUpdates; }
  // One loop pass of `d`, the main loop's: the touch sample, loop(), the frame's input over.
  static void frame(Device& d) {
    d.input->update();
    const int before = asks();
    d.activity->loop();
    d.input->clear();
    if (asks() > before) d.asked = true;
  }
  static void render(Device& d) {
    d.asked = false;
    d.activity->render(RenderLock(*d.activity));
  }
  // One pass of each device that loops, drawing what its loop asked for.
  void step() {
    for (Device* d : {host.get(), guest.get()}) {
      if (!d || !d->loops) continue;
      frame(*d);
      if (d->asked) render(*d);
    }
  }
  bool run(const std::function<bool()>& done, const int capRealMs = 10000) {
    return nearby::waitFor(
        [&] {
          step();
          return done();
        },
        capRealMs);
  }
  // `polls` passes of the loop, for a test that waits for nothing to happen.
  void idle(const int polls) {
    int left = polls;
    nearby::waitFor([&] {
      step();
      return --left <= 0;
    });
  }

  // ---- reading a device ----

  static bool shows(const Device& d, const std::string& part) {
    return !d.renderer->shown.empty() && match::holds(d.renderer->shown.back(), part);
  }
  static bool showsTurn(const Device& d) {
    return shows(d, "Player " + std::to_string(d.seat) + " (" + (d.seat == 1 ? "X" : "O") + ") to move");
  }
  // The match's state as its last transition logged it.
  static std::string state(const Device& d) {
    std::string found = "Starting";
    const std::string prefix = "INF GAME: " + d.id + ": ";
    for (const std::string& line : fakelog::snapshot()) {
      if (line.rfind(prefix, 0) != 0) continue;
      const size_t arrow = line.find(" -> ");
      const size_t on = line.find(" on ", arrow);
      if (arrow == std::string::npos || on == std::string::npos) continue;
      found = line.substr(arrow + 4, on - arrow - 4);
    }
    return found;
  }
  // How many lines the game on `d` logged holding `part` (the id is the origin of a ch.log line).
  static size_t logged(const Device& d, const std::string& part) {
    return fakelog::countLines("INF " + d.id + ": " + part);
  }
  // How many lines the match on `d` itself logged holding `part`.
  static size_t matchLogged(const Device& d, const std::string& part) {
    return fakelog::countLines("INF GAME: " + d.id + ": " + part);
  }
  // The STATEs `d`'s sink took from its peer so far (queued, and replaced in a queued one).
  static uint32_t statesReceived(const Device& d) {
    const NearbySession::Counters counters = d.session->counters();
    return counters.queued + counters.replaced;
  }

  // ---- playing ----

  // Taps the middle of cell `cell` (1..9, row by row: 140 px squares from (27, 200)) of `d`'s canvas.
  static void scriptTapCell(Device& d, const int cell) {
    d.input->tap(CANVAS_X + 27 + (cell - 1) % 3 * 140 + 70, CANVAS_Y + 200 + (cell - 1) / 3 * 140 + 70);
  }
  void tapCell(Device& d, const int cell) {
    scriptTapCell(d, cell);
    step();
  }
  // `mover` places a mark on `cell` and both devices show the result: the mover waits for the other seat, the other
  // device shows its own turn.
  void move(Device& mover, Device& other, const int cell) {
    ASSERT_TRUE(run([&] { return showsTurn(mover); })) << "seat " << int(mover.seat) << " does not show its turn";
    tapCell(mover, cell);
    ASSERT_TRUE(run([&] { return showsTurn(other) && !showsTurn(mover); })) << "cell " << cell;
  }
  // The moves of a round `cells` (seat 1 first, then alternating); the last one is expected to end it.
  void playRound(const std::vector<int>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
      Device& mover = i % 2 == 0 ? *host : *guest;
      Device& other = i % 2 == 0 ? *guest : *host;
      if (i + 1 < cells.size()) {
        ASSERT_NO_FATAL_FAILURE(move(mover, other, cells[i]));
        continue;
      }
      ASSERT_TRUE(run([&] { return showsTurn(mover); }));
      tapCell(mover, cells[i]);
    }
    ASSERT_TRUE(run([&] { return state(*host) == "Over" && state(*guest) == "Over"; })) << "the round did not end";
  }
  // X takes 1, 2, 3 and O 4, 5: seat 1's third move ends the round, and the board reads 111220000.
  static std::vector<int> hostWins() { return {1, 4, 2, 5, 3}; }
  static constexpr const char* HOST_WINS_BOARD = "111220000";

  // The settings the manifest of both devices declares (the host is given values for them, the guest none).
  uint8_t declaredSettings = 0;
  std::unique_ptr<Duo> duo;  // declared first: destroyed after the devices, whose sessions use its links
  std::unique_ptr<Device> host;
  std::unique_ptr<Device> guest;
};

std::string cardPath(const std::string& id, const char* file) { return "/.games-data/" + id + "/" + file; }

// ---- a round to over ----

TEST_F(NearbyMatchTest, ARoundIsPlayedToOverWithEachMoveAppliedOnlyOnItsMoversDevice) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  EXPECT_EQ(logged(*host, "setup mode nearby seats 2"), 1u) << "the host runs setup, with ctx.mode nearby";
  EXPECT_EQ(logged(*guest, "setup"), 0u) << "the guest never runs setup (entry 7, C9)";
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));

  // Seats 1, 3, 5 moved on the host, 2 and 4 on the guest, each applied only where it was made.
  EXPECT_EQ(logged(*host, "apply seat 1"), 3u);
  EXPECT_EQ(logged(*host, "apply seat 2"), 0u);
  EXPECT_EQ(logged(*guest, "apply seat 2"), 2u);
  EXPECT_EQ(logged(*guest, "apply seat 1"), 0u);
  // One over event each, on a board both devices hold alike.
  EXPECT_EQ(logged(*host, std::string("over for seat 1 board ") + HOST_WINS_BOARD), 1u);
  EXPECT_EQ(logged(*guest, std::string("over for seat 2 board ") + HOST_WINS_BOARD), 1u);
  EXPECT_EQ(logged(*host, "over for seat"), 1u);
  EXPECT_EQ(logged(*guest, "over for seat"), 1u);
  // Both draw the finished round, each for its own seat.
  ASSERT_TRUE(
      run([&] { return shows(*host, "Player 1 (X): Player 1 wins") && shows(*guest, "Player 2 (O): Player 1 wins"); }));
  EXPECT_EQ(state(*host), "Over");
  EXPECT_EQ(state(*guest), "Over");
}

TEST_F(NearbyMatchTest, SeatTwosMoveEndsTheRoundAndSeatOneReachesOverOnce) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  // X takes 1, 2, 9; O takes 4, 5, 6: seat 2's third move wins.
  ASSERT_NO_FATAL_FAILURE(playRound({1, 4, 2, 5, 9, 6}));
  EXPECT_EQ(logged(*host, "over for seat"), 1u);
  EXPECT_EQ(logged(*guest, "over for seat"), 1u);
  ASSERT_TRUE(
      run([&] { return shows(*host, "Player 1 (X): Player 2 wins") && shows(*guest, "Player 2 (O): Player 2 wins"); }));
  EXPECT_EQ(matchLogged(*host, "Playing -> Over"), 1u);
  EXPECT_EQ(matchLogged(*guest, "Playing -> Over"), 1u);
}

// ---- waiting for the host ----

TEST_F(NearbyMatchTest, TheGuestWaitsForTheHostTakesNoTapAndDrawsNoFrameUntilTheFirstState) {
  installBoth();
  ASSERT_NO_FATAL_FAILURE(startDuo());
  enterGuest();  // the host's match is not entered: seat 1's VM has not started
  ASSERT_TRUE(run([&] { return shows(*guest, "Waiting for the host"); }));
  EXPECT_EQ(state(*guest), "Playing");
  ASSERT_EQ(guest->renderer->shown.size(), 1u);
  EXPECT_EQ(guest->renderer->shown[0].mode, HalDisplay::FULL_REFRESH) << "drawn once, in full";
  EXPECT_FALSE(shows(*guest, "Noughts and crosses")) << "no frame of the game";

  // A tap on the board is dropped: it never reaches input.
  scriptTapCell(*guest, 5);
  idle(60);
  EXPECT_EQ(logged(*guest, "tap for seat"), 0u);
  EXPECT_EQ(guest->renderer->shown.size(), 1u) << "nothing was drawn meanwhile";
  EXPECT_EQ(logged(*guest, "setup"), 0u);

  // The host's match starts, and its first STATE replaces the waiting screen with the guest's own seat.
  enterHost();
  ASSERT_TRUE(run([&] { return shows(*guest, "Player 2 (O): Player 1 to move"); }));
  EXPECT_FALSE(shows(*guest, "Waiting for the host"));
  EXPECT_EQ(matchLogged(*guest, "Round started"), 0u) << "the VM's lines are GAME-origin, not the match's";
  EXPECT_EQ(logged(*guest, "setup"), 0u);
  EXPECT_EQ(logged(*host, "setup"), 1u);
  // And now the board takes its taps (the guest's turn is after the host's move).
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 5));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 1));
  EXPECT_EQ(logged(*guest, "apply seat 2 cell 1"), 1u);
}

TEST_F(NearbyMatchTest, ABackPressWhileWaitingOpensThePauseMenuAndResumeShowsTheWaitingScreenAgain) {
  installBoth();
  ASSERT_NO_FATAL_FAILURE(startDuo());
  enterGuest();
  ASSERT_TRUE(run([&] { return shows(*guest, "Waiting for the host"); }));
  guest->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*guest) == "Paused" && shows(*guest, tr(STR_GAMES_PAUSED)); }));
  EXPECT_FALSE(shows(*guest, "Waiting for the host"));
  guest->input->click(Button::Confirm);  // Resume
  ASSERT_TRUE(run([&] { return state(*guest) == "Playing" && shows(*guest, "Waiting for the host"); }));
  const size_t pushes = guest->renderer->shown.size();
  EXPECT_EQ(guest->renderer->shown.back().mode, HalDisplay::FULL_REFRESH) << "it replaced a menu: drawn in full";
  // A repeat of the screen already on the panel (the light panel closed) is a fast refresh.
  render(*guest);
  ASSERT_EQ(guest->renderer->shown.size(), pushes + 1);
  EXPECT_EQ(guest->renderer->shown.back().mode, HalDisplay::FAST_REFRESH);
}

// ---- a rejected move, a tap out of turn, a script error ----

TEST_F(NearbyMatchTest, ARejectedMoveReachesTheMoversInputAndSendsNothingOnEitherDevice) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 5));

  // The guest taps the taken square: rejected on the guest, nothing shipped, and its turn stays.
  const uint32_t hostReceived = statesReceived(*host);
  scriptTapCell(*guest, 5);
  step();
  ASSERT_TRUE(run([&] { return shows(*guest, "That square is taken"); }));
  EXPECT_EQ(logged(*guest, "rejected for seat 2"), 1u);
  EXPECT_EQ(logged(*host, "rejected"), 0u);
  EXPECT_FALSE(shows(*host, "That square is taken"));
  idle(40);
  EXPECT_EQ(statesReceived(*host), hostReceived) << "a rejected move ships no STATE";
  EXPECT_TRUE(showsTurn(*guest)) << "the turn stays";
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 1));

  // The same on the host: it taps the guest's mark on 1.
  const uint32_t guestReceived = statesReceived(*guest);
  scriptTapCell(*host, 1);
  step();
  ASSERT_TRUE(run([&] { return shows(*host, "That square is taken"); }));
  EXPECT_EQ(logged(*host, "rejected for seat 1"), 1u);
  EXPECT_EQ(logged(*guest, "rejected for seat 1"), 0u);
  idle(40);
  EXPECT_EQ(statesReceived(*guest), guestReceived);
  EXPECT_TRUE(showsTurn(*host));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 9));
}

TEST_F(NearbyMatchTest, ATapOutOfTurnRunsInputAndShipsNothing) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  // The host holds the turn: the guest's tap reaches its input, and the returned move is discarded.
  const uint32_t hostReceived = statesReceived(*host);
  tapCell(*guest, 5);
  ASSERT_TRUE(run([&] { return logged(*guest, "tap for seat 2") == 1u; }));
  idle(40);
  EXPECT_EQ(logged(*guest, "apply"), 0u);
  EXPECT_EQ(statesReceived(*host), hostReceived);
  EXPECT_TRUE(shows(*guest, "Player 2 (O): Player 1 to move"));
  // The board is the host's to move on.
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 5));
}

TEST_F(NearbyMatchTest, AScriptErrorInApplyShowsTheErrorViewOnTheMoverAndShipsNoState) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  const uint32_t guestReceived = statesReceived(*guest);
  // The boom square, under the board (canvas 27..86 x 700..759).
  host->input->tap(CANVAS_X + 40, CANVAS_Y + 710);
  step();
  ASSERT_TRUE(run([&] { return state(*host) == "Error"; }));
  ASSERT_TRUE(run([&] { return shows(*host, tr(STR_GAMES_ERROR)); }));
  idle(40);
  EXPECT_EQ(statesReceived(*guest), guestReceived) << "no STATE is shipped for a move whose apply failed";
  EXPECT_EQ(state(*guest), "Playing");
  EXPECT_TRUE(shows(*guest, "Player 2 (O): Player 1 to move")) << "the other device is told by entry 15, not here";
}

// ---- Play again ----

TEST_F(NearbyMatchTest, TheHostsPlayAgainStartsTheNextRoundOnBothAndTheGuestsMenuOffersOnlyLeave) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  // The host's end-of-round menu is the solo one; the guest's is Leave alone, under "Waiting for the host".
  ASSERT_TRUE(run([&] { return shows(*host, tr(STR_GAMES_PLAY_AGAIN)) && shows(*host, tr(STR_GAMES_LEAVE)); }));
  EXPECT_FALSE(shows(*host, "Waiting for the host"));
  ASSERT_TRUE(run([&] { return shows(*guest, tr(STR_GAMES_LEAVE)) && shows(*guest, "Waiting for the host"); }));
  EXPECT_FALSE(shows(*guest, tr(STR_GAMES_PLAY_AGAIN))) << "only the host plays again";
  // Nothing the guest presses on its own starts a round: Back does nothing in the end-of-round menu.
  guest->input->click(Button::Back);
  idle(20);
  EXPECT_EQ(state(*guest), "Over");

  host->input->click(Button::Confirm);  // Play again, the focused first row
  ASSERT_TRUE(run([&] { return state(*host) == "Playing" && showsTurn(*host); }));
  ASSERT_TRUE(run([&] { return state(*guest) == "Playing" && shows(*guest, "Player 2 (O): Player 1 to move"); }))
      << "the guest leaves Over by itself";
  EXPECT_EQ(logged(*host, "setup mode nearby"), 2u);
  EXPECT_EQ(logged(*guest, "setup"), 0u);
  EXPECT_EQ(matchLogged(*guest, "Over -> Playing on PlayAgain"), 1u);
  // The new round is played as the first was, with ver counting on.
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  EXPECT_EQ(logged(*host, "over for seat"), 2u);
  EXPECT_EQ(logged(*guest, "over for seat"), 2u);
}

TEST_F(NearbyMatchTest, AGuestThatAdoptedTheOverAndTheNextRoundBeforeItsLoopLookedStillShowsBoth) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 1));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 4));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 2));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 5));
  ASSERT_TRUE(run([&] { return showsTurn(*host); }));
  // The guest's loop is held (an overlay) while the host ends the round and starts the next: both STATEs queue (the
  // queue never replaces an `over`), and the guest adopts both before its loop looks at either.
  guest->loops = false;
  tapCell(*host, 3);
  ASSERT_TRUE(run([&] { return state(*host) == "Over"; }));
  host->input->click(Button::Confirm);  // Play again
  ASSERT_TRUE(run([&] { return state(*host) == "Playing" && logged(*host, "setup") == 2u; }));
  ASSERT_TRUE(run([&] { return guest->session->counters().queueSize >= 2; }));
  EXPECT_EQ(state(*guest), "Playing");
  guest->loops = true;
  ASSERT_TRUE(run([&] {
    return state(*guest) == "Playing" && shows(*guest, "Player 2 (O): Player 1 to move") &&
           matchLogged(*guest, "Over -> Playing on PlayAgain") == 1u;
  }));
  EXPECT_EQ(matchLogged(*guest, "Playing -> Over on RoundOver"), 1u) << "the finished round was entered";
  EXPECT_EQ(logged(*guest, "over for seat 2"), 1u);
  // The new round is playable.
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 5));
}

// ReliableLink::send answers true once it has queued the frame, which the GameLink task does at its next step; until
// then the Session's shipment is pending and the VM must leave Play again alone. The host's task steps every 1,000
// fake ms here (the guest's every 2; the fake clock runs 2 ms for each real one while the test waits, so about 500 real
// ms, an estimate until a device run), and it receives the guest's last move in a step: the host's final move, its
// Over, and the click all come within a few fake milliseconds after that step, 1,000 before the next, so the over STATE
// has not gone.
TEST_F(NearbyMatchTest, PlayAgainWaitsWhileTheOverStateHasNotGoneAndThenBothDevicesSeeBothRounds) {
  installBoth();
  duo = std::make_unique<Duo>();
  NearbySession::Options slowTask = nearby::fastOptions();
  slowTask.stepMs = 1000;
  slowTask.joinMs = 5000;  // the destructor waits for a step to end
  // A silence limit well over the host's slow steps, so the slow task does not read the guest as gone.
  duo->openHost(nearby::fastSettings(20000), slowTask);
  duo->openGuest(nearby::fastSettings(20000));
  ASSERT_TRUE(duo->joinAndStart());
  enterHost();
  enterGuest();
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 1));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 4));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 2));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 5));
  ASSERT_TRUE(run([&] { return showsTurn(*host); }));
  tapCell(*host, 3);
  ASSERT_TRUE(run([&] { return state(*host) == "Over"; }));
  const size_t sentBefore = nearby::linkLines("state sent");
  host->input->click(Button::Confirm);  // Play again, asked while the over STATE has not gone
  idle(15);
  ASSERT_EQ(nearby::linkLines("state sent"), sentBefore)
      << "the over STATE went before the click: the window was missed";
  EXPECT_EQ(logged(*host, "setup"), 1u) << "the next round waits for the over STATE";
  EXPECT_EQ(state(*guest), "Playing") << "the guest has had no STATE yet";
  // The task steps: the over STATE goes first, and then the next round starts.
  ASSERT_TRUE(run([&] {
    return logged(*host, "setup") == 2u && state(*guest) == "Playing" &&
           shows(*guest, "Player 2 (O): Player 1 to move") && matchLogged(*guest, "Over -> Playing on PlayAgain") == 1u;
  }));
  EXPECT_EQ(logged(*guest, "over for seat 2"), 1u) << "the guest saw the finished round, not only the next";
  EXPECT_EQ(matchLogged(*guest, "Playing -> Over on RoundOver"), 1u);
}

// The host's Play again runs setup, so only it needs the settings the manifest declares (handle()'s guard): a guest
// that was given none must still leave Over for the host's next round, not end in the error view.
TEST_F(NearbyMatchTest, TheGuestNeedsNoSettingsToFollowTheHostsPlayAgain) {
  declaredSettings = 1;
  GameCore::SettingValues values;
  std::snprintf(values.entries[0].id, sizeof(values.entries[0].id), "level");
  std::snprintf(values.entries[0].value, sizeof(values.entries[0].value), "easy");
  values.count = 1;
  installBoth();
  ASSERT_NO_FATAL_FAILURE(startDuo());
  enterHost(GameMatchActivity::Start::New, values);
  enterGuest();  // settings.count 0 against a manifest that declares 1
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  host->input->click(Button::Confirm);  // Play again
  ASSERT_TRUE(run([&] {
    return state(*guest) == "Playing" && shows(*guest, "Player 2 (O): Player 1 to move") &&
           matchLogged(*guest, "Over -> Playing on PlayAgain") == 1u;
  }));
  EXPECT_EQ(state(*host), "Playing");
}

// The over event is input() too: a script error in it on the guest ends the guest's VM (the error view), while the
// host, whose own over event is fine, plays on to Over.
TEST_F(NearbyMatchTest, AScriptErrorInTheAdoptedOverEventShowsTheErrorViewOnThatDeviceOnly) {
  installBoth();
  std::ifstream in(std::string(MATCH_FIXTURES_DIR) + "/nearby/main.lua", std::ios::binary);
  std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string over = "  elseif ev.kind == \"over\" then\n";
  ASSERT_NE(source.find(over), std::string::npos);
  source.replace(source.find(over), over.size(), over + "    if seat == 2 then error(\"over boom\") end\n");
  match::installGame(GUEST_ID, source);
  ASSERT_NO_FATAL_FAILURE(startDuo());
  enterHost();
  enterGuest();
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 1));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 4));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 2));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 5));
  ASSERT_TRUE(run([&] { return showsTurn(*host); }));
  tapCell(*host, 3);
  ASSERT_TRUE(run([&] { return state(*host) == "Over" && state(*guest) == "Error"; }));
  ASSERT_TRUE(run([&] { return shows(*guest, tr(STR_GAMES_ERROR)); }));
  EXPECT_TRUE(shows(*host, tr(STR_GAMES_LEAVE))) << "the host's end-of-round menu";
  EXPECT_EQ(logged(*host, "over for seat 1"), 1u);
}

// ---- an overlay ----

TEST_F(NearbyMatchTest, WhileTheLoopIsHeldQueuedStatesWaitTheLinkKeepsAckingAndTheyAreAdoptedOnReturn) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  guest->loops = false;  // an overlay: the guest's loop does not run
  const size_t pushes = guest->renderer->shown.size();
  ASSERT_TRUE(run([&] { return showsTurn(*host); }));
  tapCell(*host, 5);
  // The STATE reaches the guest's queue and is acked, but nothing adopts it.
  ASSERT_TRUE(nearby::waitFor([&] {
    step();
    return guest->session->counters().queueSize == 1 && host->session->counters().unacked == 0;
  }));
  idle(60);
  EXPECT_EQ(guest->session->counters().queueSize, 1u) << "adoption waits behind the overlay (R5)";
  EXPECT_EQ(guest->renderer->shown.size(), pushes);
  EXPECT_TRUE(shows(*guest, "Player 2 (O): Player 1 to move"));
  // The loop returns: the VM is woken, adopts, and the guest draws its turn.
  guest->loops = true;
  ASSERT_TRUE(run([&] { return showsTurn(*guest); }));
  EXPECT_EQ(guest->session->counters().queueSize, 0u);
}

// ---- nothing is saved ----

TEST_F(NearbyMatchTest, NoResumeBinIsWrittenDeletedOrReadAndAPassSaveBesideItIsUntouched) {
  // Each device's folder holds a pass save, as a pass match of the same game left it.
  const Bytes passSave = [] {
    Bytes bytes = savefile::resumeBytes(savefile::snapshotOf(2), 3, savefile::HASH_A);
    bytes[14] = 1;  // pass
    bytes[15] = 2;
    return bytes;
  }();
  installBoth(/*withPkg=*/true);
  for (const char* id : {HOST_ID, GUEST_ID}) fakesd::addFile(cardPath(id, "resume.bin"), passSave);
  ASSERT_NO_FATAL_FAILURE(startDuo());
  const size_t opsBefore = fakesd::sim().ops.size();
  // A caller that asked to resume gets a new nearby match all the same (the session's roster wins; nothing is resumed).
  enterHost(GameMatchActivity::Start::Resume);
  enterGuest(GameMatchActivity::Start::Resume);
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  host->input->click(Button::Confirm);  // Play again: a new round's first snapshot would be a save's write
  ASSERT_TRUE(run([&] { return state(*guest) == "Playing" && logged(*host, "setup") == 2u; }));
  idle(60);

  // Leave on the guest (Over's Leave), the forced exit on the host.
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  guest->input->click(Button::Confirm);  // the one row: Leave
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  activityManager.exitHolding(*host->activity);
  host->exited = true;

  for (const char* id : {HOST_ID, GUEST_ID}) {
    EXPECT_EQ(fakesd::bytesOf(cardPath(id, "resume.bin")), passSave) << id << ": the pass save is byte-identical";
    EXPECT_TRUE(fakesd::bytesOf(cardPath(id, "resume.bin.tmp")).empty()) << id;
  }
  const std::set<std::string> saves = match::resumeFilesOnCard();
  EXPECT_EQ(saves, (std::set<std::string>{cardPath(HOST_ID, "resume.bin"), cardPath(GUEST_ID, "resume.bin")}));
  const auto& ops = fakesd::sim().ops;
  for (size_t i = opsBefore; i < ops.size(); ++i) {
    EXPECT_EQ(ops[i].find("resume.bin"), std::string::npos) << "the card was asked: " << ops[i];
  }
  EXPECT_FALSE(logHas("Resuming at ver"));
}

// A caller that knows nothing of the session may ask to Resume (the title screen's Continue): a nearby match has
// nothing to resume, and without a .pkg a resume would stop in the error view rather than start over a save.
TEST_F(NearbyMatchTest, ARequestToResumeIsAnEntryToANewNearbyMatchWhateverTheCardHolds) {
  installBoth();  // no .pkg: a Resume start would refuse to play over a save it cannot tell from any other
  ASSERT_NO_FATAL_FAILURE(startDuo());
  enterHost(GameMatchActivity::Start::Resume);
  enterGuest(GameMatchActivity::Start::Resume);
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  EXPECT_EQ(state(*host), "Playing");
  EXPECT_EQ(state(*guest), "Playing");
  EXPECT_EQ(logged(*host, "setup mode nearby"), 1u);
}

TEST_F(NearbyMatchTest, TheFixturesStoreWriteLandsOnTheMoversDeviceOnly) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 5));  // only the host has moved
  // The forced exit flushes a dirty ch.store (AD-17).
  for (Device* d : {guest.get(), host.get()}) {
    activityManager.exitHolding(*d->activity);
    d->exited = true;
  }
  const Bytes hostStore = fakesd::bytesOf(cardPath(HOST_ID, "store.bin"));
  const std::string hostText(hostStore.begin(), hostStore.end());
  EXPECT_NE(hostText.find("moves"), std::string::npos) << "the mover's store.bin holds the write";
  EXPECT_NE(hostText.find("last"), std::string::npos);
  EXPECT_TRUE(fakesd::bytesOf(cardPath(GUEST_ID, "store.bin")).empty()) << "the other device wrote nothing";
}

// ---- the session, the screen, the doubles ----

TEST_F(NearbyMatchTest, ANearbyMatchHoldsTheDeviceAwakeAndASoloOneDoesNot) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  EXPECT_TRUE(host->activity->preventAutoSleep());
  EXPECT_TRUE(guest->activity->preventAutoSleep());
  GfxRenderer soloRenderer(480, 800);
  MappedInputManager soloInput(gpio, soloRenderer);
  GameMatchActivity solo(soloRenderer, soloInput, match::manifestOf("solo"), GameCore::Roster::solo());
  EXPECT_FALSE(solo.preventAutoSleep());
}

TEST_F(NearbyMatchTest, ASessionWithoutAMatchEndsInTheErrorView) {
  installBoth();
  duo = std::make_unique<Duo>();
  duo->openBoth();  // joined but never started: beginMatch() is false
  enterHost();
  EXPECT_EQ(state(*host), "Error");
  EXPECT_TRUE(logHas("the nearby session has no match to play"));
  ASSERT_TRUE(run([&] { return shows(*host, tr(STR_GAMES_START_FAILED)); }));
}

TEST_F(NearbyMatchTest, TheHostsFirstStateIsShippedByTheVmWithoutAnyInput) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  // The first STATE went out from the VM's own pass, woken by the loop's poll: no tap or timer was posted.
  EXPECT_GE(statesReceived(*guest), 1u);
  EXPECT_EQ(logged(*host, "tap for seat"), 0u);
}

TEST_F(NearbyMatchTest, TheTwoDevicesShareNoGameFiles) {
  match::installFixtureAs("nearby", HOST_ID);
  match::installFixtureAs("nearby", GUEST_ID);
  match::installFixture("nearby");
  const Bytes source = fakesd::bytesOf("/.games/nearby/main.lua");
  ASSERT_FALSE(source.empty());
  EXPECT_EQ(fakesd::bytesOf(std::string("/.games/") + HOST_ID + "/main.lua"), source);
  EXPECT_EQ(fakesd::bytesOf(std::string("/.games/") + GUEST_ID + "/main.lua"), source);
  EXPECT_EQ(fakesd::bytesOf(std::string("/.games/") + HOST_ID + "/manifest.json"),
            fakesd::bytesOf("/.games/nearby/manifest.json"));
  fakesd::addFile(cardPath(HOST_ID, "store.bin"), std::string("x"));
  EXPECT_TRUE(fakesd::bytesOf(cardPath(GUEST_ID, "store.bin")).empty());
}

}  // namespace

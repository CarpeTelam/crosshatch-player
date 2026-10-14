#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
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
    rig.reset();
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
  // Every text of `d`'s last push, one per line (for a failure message).
  static std::string screenText(const Device& d) {
    std::string text;
    if (d.renderer->shown.empty()) return text;
    for (const std::string& line : d.renderer->shown.back().texts) text += line + "\n";
    return text;
  }
  // A line the view wraps over its two lines still reads as one: the last push's texts joined by spaces hold `line`.
  static bool showsLine(const Device& d, const std::string& line) {
    if (d.renderer->shown.empty()) return false;
    std::string joined;
    for (const std::string& text : d.renderer->shown.back().texts) joined += text + " ";
    return joined.find(line) != std::string::npos;
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
  // How many times the match on `d` entered `state` (its transition lines, "<id>: A -> <state> on E").
  static size_t entered(const Device& d, const std::string& state) {
    size_t count = 0;
    const std::string prefix = "INF GAME: " + d.id + ": ";
    for (const std::string& line : fakelog::snapshot()) {
      if (line.rfind(prefix, 0) == 0 && line.find(" -> " + state + " on ") != std::string::npos) ++count;
    }
    return count;
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

  // ---- the endings (entry 15) ----

  // The index of the first log line holding `part` at or after `from`; npos when none.
  static size_t lineAt(const std::string& part, const size_t from = 0) {
    const std::vector<std::string> lines = fakelog::snapshot();
    for (size_t i = from; i < lines.size(); ++i) {
      if (lines[i].find(part) != std::string::npos) return i;
    }
    return std::string::npos;
  }
  // Every LINK and GAME log line, one per line (for a failure message).
  static std::string linkDump() {
    std::string text;
    for (const std::string& line : fakelog::snapshot()) {
      if (line.find("LINK:") != std::string::npos || line.find("GAME:") != std::string::npos) text += line + "\n";
    }
    return text;
  }
  // The `waited=` of the last `leave reason=` line the sessions logged, in (fake) milliseconds; -1 when none.
  static long lastLeaveWaitedMs() {
    long waited = -1;
    for (const std::string& line : fakelog::snapshot()) {
      const size_t at = line.find("leave reason=");
      const size_t w = line.find("waited=", at);
      if (at != std::string::npos && w != std::string::npos) waited = std::stol(line.substr(w + 7));
    }
    return waited;
  }
  // The match's pause menu is up on `d` (Back opens it).
  bool openPauseMenu(Device& d) {
    d.input->click(Button::Back);
    return run([&] { return state(d) == "Paused" && shows(d, tr(STR_GAMES_PAUSED)); });
  }
  // Chooses Leave in the pause menu (the second row) on `d`.
  void leaveFromPauseMenu(Device& d) {
    ASSERT_TRUE(openPauseMenu(d));
    d.input->click(Button::NavNext);
    step();
    d.input->click(Button::Confirm);
    ASSERT_TRUE(run([&] { return state(d) == "Leaving"; }));
  }
  // What each link's end() count was at every goToGames() ask, in order: {host, guest}.
  std::vector<std::pair<int, int>> endsAtGames;
  void recordEndsAtGames() {
    activityManager.onGoToGames = [this] {
      endsAtGames.emplace_back(duo->hostProbe.endCalls, duo->guestProbe.endCalls);
    };
  }
  // A guest device on a raw host that the test speaks for (nearby::RawHostRig): installs the fixtures, brings the guest
  // session up with `silenceMs` as its silence limit, and enters the match on it. Only the guest exists.
  void startRawGuest(const uint32_t silenceMs = 10000) {
    installBoth();
    rig = std::make_unique<nearby::RawHostRig>(silenceMs);
    ASSERT_TRUE(rig->open());
    ASSERT_TRUE(rig->joinAndStart());
    rawHeardAt = fakertos::millis();  // the raw host sends nothing after its ACCEPT, so it was last heard by now
    guest = enter(GUEST_ID, 2, std::move(rig->guest));
    ASSERT_TRUE(run([&] { return shows(*guest, "Waiting for the host"); }));
  }
  // Moves the fake clock to `rawHeardAt + sinceHeardMs` in steps of at most one second, so a call the VM happens to be
  // inside never spans the 3 s watchdog (the fake clock is the match's millis()), letting the tasks run between steps.
  void advanceToSinceHeard(const uint64_t sinceHeardMs) {
    while (fakertos::millis() < rawHeardAt + sinceHeardMs) {
      fakertos::advance(std::min<uint64_t>(1000, rawHeardAt + sinceHeardMs - fakertos::millis()));
      idle(5);
    }
  }
  // ---- a stuck VM (deferred-work ## 6.9, ## 6.15 item 1) ----

  // Holds the host's VM inside the game's "tap for seat 1" ch.log line. The gate at a log line stands in for a script
  // stuck in a host call inside one of GameVM's locked bindings, so neither the stop nor GameVM::abandon can end the
  // task ("VM stuck and not safely deletable"), as on the device. It holds VM tasks only: the GameLink tasks run on, as
  // they do on the device (held at their own LINK lines, under the session's lock, they would stall the goodbye and
  // the session's teardown). The guest's VM is idle and logs nothing meanwhile. The abandoned task may still call
  // adoptNext and ship on the session, so the match must keep that unfreed and its link up.
  void holdHostVmInACall() {
    expectCleanPsram = false;  // the VM, the store slot, and the session are leaked on purpose
    fakertos::arm(fakertos::At::Log, 0, "GameVM");
    scriptTapCell(*host, 5);
    frame(*host);
    ASSERT_TRUE(fakertos::waitParked());
  }
  // The host's screen goes as the manager takes it down: onExit (unless it ran), then the destructor, under the lock.
  void destroyHost() {
    if (!host->exited) activityManager.exitHolding(*host->activity);
    host->exited = true;
    activityManager.destroyHolding(host->activity);
  }
  // After the host's activity has gone and leaked its session: the held VM task is let go (it ends at its next hook,
  // its cancel flag set), the guest goes as usual, and the leaked session's GameLink task, which nothing stops, is
  // deleted (FakeRtos parks a deleted task for good). The Duo, whose net, links, probes, and clock the leaked session
  // still holds, cannot be freed: it is leaked with the session.
  void endTheLeakedTasks() {
    fakertos::release();
    activityManager.exitHolding(*guest->activity);
    guest->exited = true;
    activityManager.destroyHolding(guest->activity);
    ASSERT_TRUE(match::waitFor([] { return fakertos::S().liveTasks.load() == 1; })) << "the leaked VM task ran on";
    fakertos::Task* link = nullptr;
    {
      std::lock_guard<std::mutex> lock(fakertos::S().m);
      for (const auto& task : fakertos::S().tasks) {
        if (task->alive && task->name == "GameLink") link = task.get();
      }
    }
    ASSERT_NE(link, nullptr) << "the leaked session's GameLink task";
    vTaskDelete(link);
    (void)duo.release();
  }

  // The reason line the player-left view shows, and the match's own log line for the reason.
  static std::string leftLine() { return tr(STR_GAMES_PEER_LEFT); }
  static std::string goneLogged(const char* reason) { return std::string("the other player is gone: ") + reason; }

  // The settings the manifest of both devices declares (the host is given values for them, the guest none).
  uint8_t declaredSettings = 0;
  uint64_t rawHeardAt = 0;                  // startRawGuest: the fake time the raw host was last heard
  std::unique_ptr<nearby::RawHostRig> rig;  // declared before the devices: destroyed after them, their link's net
  std::unique_ptr<Duo> duo;                 // destroyed after the devices, whose sessions use its links
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
  // The other device is told at once (entry 15): three ABORT(script_error), and it shows the game-error line.
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3);
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, tr(STR_GAMES_PEER_ERROR)); }));
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
  EXPECT_EQ(logged(*host, "over for seat 1"), 1u);
  // The guest told its peer at once (entry 15), so the host leaves its end-of-round menu for the player-left view, with
  // the game-error line; it is in Over until the ABORT reaches it, which no assertion here depends on.
  EXPECT_EQ(entered(*host, "Over"), 1u);
  ASSERT_TRUE(run([&] { return state(*host) == "PeerGone" && showsLine(*host, tr(STR_GAMES_PEER_ERROR)); }));
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

// ---- the endings (entry 15): Leave, the forced exit, a script error, and the player-left view ----

// Test doubles of this section are the suite's own (FakeLink, ObservedLink, RawHostRig: NearbySessionSupport.h's
// header lists the device behaviour each stands in for). ObservedLink::end() counts the radio going down; the
// ActivityManager double's onGoToGames hook reads it at the moment Games is asked for, which is the order AD-20 gives
// (flush and ABORT, VM stop, radio down, then Games). Host timing bounds are fake milliseconds, estimates until a
// device run.

TEST_F(NearbyMatchTest, LeaveOnTheHostFlushesSendsThreeAbortsEndsTheLinkAndAsksForGamesWhileTheGuestShowsPlayerLeft) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  recordEndsAtGames();
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*host));
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3);
  EXPECT_GE(lastLeaveWaitedMs(), 0);
  EXPECT_LT(lastLeaveWaitedMs(), 800) << "a reachable peer acks: no full wait";
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  ASSERT_EQ(endsAtGames.size(), 1u);
  EXPECT_EQ(endsAtGames[0].first, 1) << "the host's link was ended before Games was asked for";
  // The goodbye went before the VM stopped (AD-20).
  const size_t said = lineAt("leave reason=left");
  ASSERT_NE(said, std::string::npos);
  const size_t stopped = lineAt("VM stopped");
  ASSERT_NE(stopped, std::string::npos);
  EXPECT_LT(said, stopped);

  // The guest shows the player-left view with the left line, and Back leaves in turn.
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, leftLine()) && shows(*guest, tr(STR_BACK)); }));
  EXPECT_EQ(matchLogged(*guest, goneLogged("left")), 1u);
  EXPECT_EQ(activityManager.asks.goToGames, 1) << "PeerGone waits for the player's Back";
  guest->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_EQ(activityManager.asks.goToGames, 2);
  ASSERT_EQ(endsAtGames.size(), 2u);
  EXPECT_EQ(endsAtGames[1].second, 1) << "the guest's link was ended before Games was asked for";
  EXPECT_EQ(duo->guestProbe.abortFrames.load(), 0)
      << "Back from the player-left view sent ABORTs to a peer it heard leave";
  EXPECT_EQ(nearby::linkLines(" copies=0"), 1u) << linkDump();
}

TEST_F(NearbyMatchTest, LeaveOnTheGuestEndsTheHostsMatchInTheSameWay) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  recordEndsAtGames();
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*guest));
  EXPECT_EQ(duo->guestProbe.abortFrames.load(), 3);
  ASSERT_EQ(endsAtGames.size(), 1u);
  EXPECT_EQ(endsAtGames[0].second, 1);
  ASSERT_TRUE(run([&] { return state(*host) == "PeerGone" && showsLine(*host, leftLine()); }));
  EXPECT_EQ(matchLogged(*host, goneLogged("left")), 1u);
  host->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*host) == "Leaving"; }));
  EXPECT_EQ(activityManager.asks.goToGames, 2);
}

TEST_F(NearbyMatchTest, LeaveFromTheEndOfRoundMenuAlsoSaysGoodbyeAndThePeerLeavesOverForPeerGone) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(playRound(hostWins()));
  ASSERT_TRUE(run([&] { return shows(*guest, tr(STR_GAMES_LEAVE)); }));
  guest->input->click(Button::Confirm);  // the follower's one row: Leave
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_EQ(duo->guestProbe.abortFrames.load(), 3);
  // The host was in Over, and the end moves it from there.
  ASSERT_TRUE(run([&] { return state(*host) == "PeerGone" && showsLine(*host, leftLine()); }));
  EXPECT_EQ(matchLogged(*host, "Over -> PeerGone on PeerGone"), 1u);
}

// With a STATE unacked (the host's link loses everything it sends) the flush ends at 800 ms, not before, and the three
// ABORTs go all the same; the peer learns by silence.
TEST_F(NearbyMatchTest, LeaveWithAStateUnackedFlushesForTheLimitAndSendsTheAbortsAnyway) {
  installBoth();
  duo = std::make_unique<Duo>();
  duo->openBoth(nearby::fastSettings(60000));
  ASSERT_TRUE(duo->joinAndStart());
  enterHost();
  enterGuest();
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  duo->net.blackhole(duo->hostEnd.index());
  const size_t sentBefore = nearby::linkLines("state sent");
  tapCell(*host, 5);
  // The move's STATE is with the link (the VM shipped it and the task carried it: "state sent") and, lost, stays
  // unacked. `unacked >= 1` alone would not do: an ack still in flight for the round's first STATE satisfies it, and
  // when it lands, with the move not yet shipped, the flush finds nothing to wait for (waited=0).
  ASSERT_TRUE(
      run([&] { return nearby::linkLines("state sent") > sentBefore && host->session->counters().unacked >= 1; }));
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*host));
  EXPECT_GE(lastLeaveWaitedMs(), 800) << "never before the limit\n" << linkDump();
  EXPECT_LT(lastLeaveWaitedMs(), 1100) << "and at it, not long after";
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3) << "the goodbye goes all the same";
  EXPECT_EQ(activityManager.asks.goToGames, 1);
}

TEST_F(NearbyMatchTest, TheForcedExitPutsThreeAbortsOnTheLinkAtOnceBeforeTheVmStopsAndThePeerShowsPlayerLeft) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3) << "sent before onExit returned";
  EXPECT_EQ(lastLeaveWaitedMs(), 0) << "no wait";
  const size_t said = lineAt("leave reason=left waited=0ms");
  ASSERT_NE(said, std::string::npos);
  const size_t stopped = lineAt("VM stopped");
  ASSERT_NE(stopped, std::string::npos);
  EXPECT_LT(said, stopped) << "before the VM stops";
  EXPECT_EQ(activityManager.asks.goToGames, 0) << "a forced exit does not ask for Games";
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone" && showsLine(*guest, leftLine()); }));
}

// A Leave that ran, whose Games screen could not be built (goToGames() recorded and replaced nothing), leaves the match
// current: its forced exit later says nothing more.
TEST_F(NearbyMatchTest, AForcedExitAfterALeaveSendsNoSecondGoodbye) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*host));
  const int aborts = duo->hostProbe.abortFrames.load();
  EXPECT_EQ(aborts, 3);
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), aborts);
}

TEST_F(NearbyMatchTest, AScriptErrorOnTheMoverSendsThreeAbortsAtOnceAndThePeersViewHasTheGameErrorLine) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  recordEndsAtGames();
  host->input->tap(CANVAS_X + 40, CANVAS_Y + 710);  // the boom square
  step();
  ASSERT_TRUE(run([&] { return state(*host) == "Error"; }));
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3);
  EXPECT_NE(lineAt("leave reason=script_error waited=0ms"), std::string::npos);
  EXPECT_EQ(activityManager.asks.goToGames, 0) << "the erroring device stays on its error view";
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone" && showsLine(*guest, tr(STR_GAMES_PEER_ERROR)); }));
  EXPECT_EQ(matchLogged(*guest, goneLogged("script_error")), 1u);

  // The peer leaves from its player-left view, sending nothing (it heard the host's ABORT): the error view stays, and
  // the host never reads the end the peer's silence latches.
  guest->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_EQ(duo->guestProbe.abortFrames.load(), 0);
  ASSERT_TRUE(run([&] { return host->session->endLatched(); }));
  idle(60);
  EXPECT_EQ(state(*host), "Error");
  EXPECT_TRUE(shows(*host, tr(STR_GAMES_ERROR)));
  EXPECT_EQ(entered(*host, "PeerGone"), 0u);
  GameCore::Protocol::AbortReason unread = GameCore::Protocol::AbortReason::Left;
  EXPECT_TRUE(host->session->takeEnd(unread)) << "the slot was not read while the match was in Error";
  EXPECT_EQ(unread, GameCore::Protocol::AbortReason::Timeout);

  // Back from the error view leaves without waiting: three more ABORT(left), the link ended before Games is asked for.
  host->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*host) == "Leaving"; }));
  EXPECT_EQ(lastLeaveWaitedMs(), 0);
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 6);
  EXPECT_EQ(activityManager.asks.goToGames, 2) << "the guest's Back asked first";
  ASSERT_EQ(endsAtGames.size(), 2u);
  EXPECT_EQ(endsAtGames[1].first, 1) << "the host's link was ended before Games was asked for";
}

TEST_F(NearbyMatchTest, ASilentPeerEndsTheMatchAtTheSilenceLimitAndNotBefore) {
  ASSERT_NO_FATAL_FAILURE(startRawGuest(10000));
  // 9.5 s since the raw host was last heard is not the limit (10 s); 10.2 s is. Millisecond bounds in fake time.
  advanceToSinceHeard(9500);
  EXPECT_EQ(state(*guest), "Playing") << "9.5 s of silence is not the limit";
  EXPECT_FALSE(guest->session->endLatched());
  advanceToSinceHeard(10200);
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, tr(STR_GAMES_PEER_SILENT)); }));
  EXPECT_EQ(matchLogged(*guest, goneLogged("timeout")), 1u);
}

TEST_F(NearbyMatchTest, AVersionMismatchAbortShowsTheVersionLine) {
  ASSERT_NO_FATAL_FAILURE(startRawGuest());
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::VersionMismatch));
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, tr(STR_GAMES_PEER_VERSION)); })) << screenText(*guest);
  EXPECT_EQ(matchLogged(*guest, goneLogged("version_mismatch")), 1u);
}

TEST_F(NearbyMatchTest, AFullAbortShowsTheLeftLine) {
  ASSERT_NO_FATAL_FAILURE(startRawGuest());
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::Full));
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, leftLine()); }));
  EXPECT_EQ(matchLogged(*guest, goneLogged("full")), 1u);
}

TEST_F(NearbyMatchTest, ThePlayerLeftViewsLinesAreEachReasonsAndTheStringsAreTheAgreedOnes) {
  using GameCore::Protocol::AbortReason;
  EXPECT_EQ(GameMatchView::peerGoneLine(AbortReason::Left), StrId::STR_GAMES_PEER_LEFT);
  EXPECT_EQ(GameMatchView::peerGoneLine(AbortReason::Full), StrId::STR_GAMES_PEER_LEFT);
  EXPECT_EQ(GameMatchView::peerGoneLine(AbortReason::Timeout), StrId::STR_GAMES_PEER_SILENT);
  EXPECT_EQ(GameMatchView::peerGoneLine(AbortReason::ScriptError), StrId::STR_GAMES_PEER_ERROR);
  EXPECT_EQ(GameMatchView::peerGoneLine(AbortReason::VersionMismatch), StrId::STR_GAMES_PEER_VERSION);
  EXPECT_STREQ(tr(STR_GAMES_PEER_LEFT), "The other player left");
  EXPECT_STREQ(tr(STR_GAMES_PEER_SILENT), "The other player went silent");
  EXPECT_STREQ(tr(STR_GAMES_PEER_ERROR), "The other game stopped with an error");
  EXPECT_STREQ(tr(STR_GAMES_PEER_VERSION), "The other player has a different version");
  for (const StrId id : {StrId::STR_GAMES_PEER_LEFT, StrId::STR_GAMES_PEER_SILENT, StrId::STR_GAMES_PEER_ERROR,
                         StrId::STR_GAMES_PEER_VERSION}) {
    EXPECT_LT(std::strlen(I18N.get(id)), 42u) << "two lines of the title font in the dialog's 4/5 width";
  }
}

// R5: the end waits for the match's own loop(), so it waits behind an open overlay, which stops the loop.
TEST_F(NearbyMatchTest, ALatchedEndWaitsBehindAnOverlayAndTheNextLoopPassEntersPeerGone) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  guest->loops = false;  // an overlay: the guest's loop does not run
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  ASSERT_TRUE(nearby::waitFor([&] {
    step();
    return guest->session->endLatched();
  }));
  idle(60);
  EXPECT_EQ(state(*guest), "Playing") << "nothing changes while the loop is held";
  EXPECT_EQ(entered(*guest, "PeerGone"), 0u);
  guest->loops = true;
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone" && showsLine(*guest, leftLine()); }));
}

TEST_F(NearbyMatchTest, TwoReasonsGiveOnePeerGoneAndOneLogLineNamingTheFirst) {
  ASSERT_NO_FATAL_FAILURE(startRawGuest());
  guest->loops = false;
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::Left));
  ASSERT_TRUE(nearby::waitFor([&] { return guest->session->endLatched(); }));
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::VersionMismatch));
  ASSERT_TRUE(nearby::waitFor([&] { return guest->session->counters().endIgnored >= 1; }))
      << "the second reason reached the slot and was ignored";
  guest->loops = true;
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  ASSERT_TRUE(run([&] { return showsLine(*guest, leftLine()); }));
  idle(40);
  EXPECT_EQ(matchLogged(*guest, "the other player is gone"), 1u);
  EXPECT_EQ(matchLogged(*guest, goneLogged("left")), 1u);
  EXPECT_FALSE(showsLine(*guest, tr(STR_GAMES_PEER_VERSION)));
  EXPECT_EQ(entered(*guest, "PeerGone"), 1u);
}

TEST_F(NearbyMatchTest, PeerGoneIsEnteredFromPausedAsFromPlaying) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_TRUE(openPauseMenu(*guest));
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone" && showsLine(*guest, leftLine()); }));
  EXPECT_EQ(matchLogged(*guest, "Paused -> PeerGone on PeerGone"), 1u);
  EXPECT_FALSE(shows(*guest, tr(STR_GAMES_RESUME))) << "the pause menu is gone";
}

// A Leave whose VM will not stop: the goodbye goes, the VM is abandoned and leaked, and the session it may still call
// stays, unfreed, with its link up (leave()'s `!slotLeaked` guard, then the destructor's release).
TEST_F(NearbyMatchTest, ALeaveWithAStuckVmNeverEndsTheLeakedSessionsLink) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(holdHostVmInACall());
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*host));
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  EXPECT_TRUE(logHas("The ch.store slot stays with the leaked VM"));
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3) << "the goodbye goes all the same";
  EXPECT_EQ(duo->hostProbe.endCalls.load(), 0) << "Leave ended the link of the session the leaked VM may call";
  destroyHost();
  EXPECT_EQ(duo->hostProbe.endCalls.load(), 0) << "the destructor freed the session the leaked VM may call";
  ASSERT_NO_FATAL_FAILURE(endTheLeakedTasks());
}

// The same for the forced exit (onExit's goodbye, then the stop that abandons): the destructor's release keeps it.
TEST_F(NearbyMatchTest, AForcedExitWithAStuckVmNeverEndsTheLeakedSessionsLink) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(holdHostVmInACall());
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  EXPECT_EQ(duo->hostProbe.abortFrames.load(), 3);
  EXPECT_EQ(duo->hostProbe.endCalls.load(), 0);
  destroyHost();
  EXPECT_EQ(duo->hostProbe.endCalls.load(), 0) << "the destructor freed the session the leaked VM may call";
  ASSERT_NO_FATAL_FAILURE(endTheLeakedTasks());
}

TEST_F(NearbyMatchTest, ThePlayerLeftViewHasBackAloneAndNothingElseLeavesIt) {
  ASSERT_NO_FATAL_FAILURE(startRawGuest());
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::Left));
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone" && showsLine(*guest, leftLine()); }));
  EXPECT_TRUE(shows(*guest, tr(STR_BACK)));
  EXPECT_FALSE(shows(*guest, tr(STR_GAMES_LEAVE)));
  // Confirm does nothing; Home is taken and ignored.
  guest->input->click(Button::Confirm);
  idle(20);
  EXPECT_EQ(state(*guest), "PeerGone");
  EXPECT_TRUE(guest->activity->handleHomeGesture());
  idle(20);
  EXPECT_EQ(state(*guest), "PeerGone");
  EXPECT_EQ(activityManager.asks.goToGames, 0);
  // A tap on the Back row leaves, as the Back button does.
  render(*guest);
  const screen::RecordingTarget& target = *screen::RecordingTarget::newest();
  bool tapped = false;
  for (const screen::DrawnText& drawn : target.drawn) {
    if (drawn.text != tr(STR_BACK)) continue;
    guest->input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
    tapped = true;
    break;
  }
  ASSERT_TRUE(tapped) << target.joined();
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_EQ(activityManager.asks.goToGames, 1);
}

TEST_F(NearbyMatchTest, AMatchThatEndsInPeerGoneTouchesNoResumeFile) {
  installBoth(/*withPkg=*/true);
  rig = std::make_unique<nearby::RawHostRig>();
  ASSERT_TRUE(rig->open());
  ASSERT_TRUE(rig->joinAndStart());
  guest = enter(GUEST_ID, 2, std::move(rig->guest));
  ASSERT_TRUE(run([&] { return shows(*guest, "Waiting for the host"); }));
  ASSERT_TRUE(rig->putAbort(GameCore::Protocol::AbortReason::Left));
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  guest->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*guest) == "Leaving"; }));
  EXPECT_TRUE(match::resumeFilesOnCard().empty());
}

// Back from the player-left view leaves without waiting for acks from a peer already known gone, even with a STATE of
// its own still unacked, and sends that peer no ABORT: its own ABORT said it left.
TEST_F(NearbyMatchTest, BackFromThePlayerLeftViewSendsTheGoodbyeWithoutWaitingForAcks) {
  installBoth();
  duo = std::make_unique<Duo>();
  duo->openBoth(nearby::fastSettings(60000));
  ASSERT_TRUE(duo->joinAndStart());
  enterHost();
  enterGuest();
  ASSERT_TRUE(run([&] { return showsTurn(*host) && shows(*guest, "Player 2 (O): Player 1 to move"); }));
  recordEndsAtGames();
  duo->net.blackhole(duo->hostEnd.index());  // the host's sends are lost (its STATE stays unacked), the guest's arrive
  const size_t stateSent = nearby::linkLines("state sent");
  tapCell(*host, 5);
  ASSERT_TRUE(
      run([&] { return host->session->counters().unacked >= 1 && nearby::linkLines("state sent") > stateSent; }));
  ASSERT_NO_FATAL_FAILURE(leaveFromPauseMenu(*guest));  // its ABORT reaches the host
  ASSERT_TRUE(run([&] { return state(*host) == "PeerGone" && showsLine(*host, leftLine()); }));
  const int abortsBefore = duo->hostProbe.abortFrames.load();
  host->input->click(Button::Back);
  ASSERT_TRUE(run([&] { return state(*host) == "Leaving"; }));
  EXPECT_EQ(lastLeaveWaitedMs(), 0) << "no wait for acks from a peer that is gone";
  EXPECT_EQ(duo->hostProbe.abortFrames.load() - abortsBefore, 0) << "an ABORT went to the peer that had left";
  ASSERT_EQ(endsAtGames.size(), 2u);
  EXPECT_EQ(endsAtGames[1].first, 1) << "the host's link was ended before Games was asked for";
}

// A round end the VM has already counted is entered (Over: the over event delivered, the store flushed) before the
// loop reads the end; the end is read on the next pass.
TEST_F(NearbyMatchTest, ARoundEndTheVmHasCountedIsEnteredBeforeTheEndIsRead) {
  ASSERT_NO_FATAL_FAILURE(startMatch());
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 1));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 4));
  ASSERT_NO_FATAL_FAILURE(move(*host, *guest, 2));
  ASSERT_NO_FATAL_FAILURE(move(*guest, *host, 5));
  ASSERT_TRUE(run([&] { return showsTurn(*host); }));
  guest->loops = false;  // an overlay: the guest's loop does not run
  tapCell(*host, 3);     // the winning move: the over STATE is queued on the guest
  ASSERT_TRUE(nearby::waitFor([&] {
    step();
    return guest->session->counters().queueSize >= 1;
  }));
  // One guest pass wakes its VM (pollNearby), which adopts the over STATE and delivers the over event.
  frame(*guest);
  ASSERT_TRUE(nearby::waitFor([&] { return logged(*guest, "over for seat 2") == 1u; }));
  // The host leaves while the guest's loop is still held: the end is latched, and not yet read.
  activityManager.exitHolding(*host->activity);
  host->exited = true;
  ASSERT_TRUE(nearby::waitFor([&] { return guest->session->endLatched(); }));
  guest->loops = true;
  ASSERT_TRUE(run([&] { return state(*guest) == "PeerGone"; }));
  EXPECT_EQ(matchLogged(*guest, "Playing -> Over on RoundOver"), 1u);
  EXPECT_EQ(matchLogged(*guest, "Over -> PeerGone on PeerGone"), 1u);
  const size_t over = lineAt(std::string("INF GAME: ") + GUEST_ID + ": Playing -> Over on RoundOver");
  const size_t gone = lineAt(std::string("INF GAME: ") + GUEST_ID + ": Over -> PeerGone on PeerGone");
  ASSERT_NE(over, std::string::npos);
  ASSERT_NE(gone, std::string::npos);
  EXPECT_LT(over, gone);
}

}  // namespace

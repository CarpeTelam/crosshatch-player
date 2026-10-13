#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "ApiLevel.h"
#include "EspNowLinkScript.h"
#include "GameRegistry.h"
#include "NearbyLobbySupport.h"
#include "games/GameHostCaps.h"

// The Nearby lobby and the title screen's Nearby start (entry 10 of epic-play-nearby; R8, R9): the real
// GameLobbyActivity, GameModeActivity, GameMatchActivity, NearbySession, and GameCore, over the screen doubles, a
// FakeLink radio (NearbyLobbySupport.h), and the nearby fixture. A host and a guest lobby are two devices on one net:
// the host hosts, the guest lists it and joins, the host starts, and both lobbies are replaced by the match with the
// session moved in. The refusals (Wi-Fi in use, a short heap) and the title screen's own rules are read on the real
// title screen. Every bound is in fake milliseconds (nearby::waitFor moves the clock), never host speed; the 3 s live
// window, the 1 s refresh bound, and the 100 ms poll are estimates until the device run (entry 14).

// A nothrow allocation of exactly this many bytes fails while it is non-zero: how a test makes the lobby's
// makeUniqueNoThrow<GameMatchActivity> return null (ModePickerTest does the same for the title screen's). Every other
// allocation is malloc, as the default operator's. The sessions' tasks allocate too, so the size is atomic.
namespace oom {
std::atomic<std::size_t> failSize{0};
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  const std::size_t fail = oom::failSize.load();
  if (fail != 0 && size == fail) return nullptr;
  return std::malloc(size);
}

namespace {

using namespace nearbylobby;
using GameCore::Manifest;
using savefile::HASH_A;
using savefile::HASH_B;

constexpr uint8_t SOLO = Manifest::MODE_SOLO;
constexpr uint8_t PASS = Manifest::MODE_PASS;
constexpr uint8_t NEARBY = Manifest::MODE_NEARBY;
constexpr uint32_t RAW_SESSION = 0x48570001u;

// The label of the n-th link the radio opens ("X4 Pro 3F2A", then 3F2B, ...).
std::string labelOf(const size_t n) { return FakeNearbyRadio::labelOfOpen(n); }

std::string resumePath() { return "/.games-data/nearby/resume.bin"; }
std::string prefsPath() { return "/.games-data/nearby/prefs.bin"; }

// For the owner's review: with LOBBY_SHOTS_DIR set, writes what a screen drew as one tab-separated line per text (the
// words, then x, y, width, height, ink: the lines the screen doubles recorded, not a panel's pixels) to
// <dir>/<name>.tsv. Without it the dump tests do nothing.
void dumpScreen(const char* name, const std::vector<screen::DrawnText>& texts) {
  const char* dir = std::getenv("LOBBY_SHOTS_DIR");
  if (dir == nullptr) return;
  std::ofstream out(std::string(dir) + "/" + name + ".tsv");
  for (const screen::DrawnText& t : texts) {
    out << t.text << '\t' << t.rect.x << '\t' << t.rect.y << '\t' << t.rect.width << '\t' << t.rect.height << '\t'
        << static_cast<int>(t.color) << '\n';
  }
}

class NearbyLobbyTest : public LobbyRigTest {
 protected:
  // The header the last render drew.
  static std::string headerOf() {
    std::string header = "<none>";
    for (const auto& call : UITheme::getInstance().getTheme().calls)
      if (call.what == "drawHeader") header = call.text;
    return header;
  }

  // A host `h` hosting (its link is the radio's first if it hosts first) and a guest `g` that listed it and joined.
  void hostAndJoin(Dev& h, Dev& g, const std::string& hostLabel = labelOf(0)) {
    ASSERT_NO_FATAL_FAILURE(host(h));
    ASSERT_NO_FATAL_FAILURE(joinScreen(g));
    ASSERT_NO_FATAL_FAILURE(joinHost(g, hostLabel, h));
  }

  // Both matches running after the host's Start (Start is enabled and selected once a guest joined).
  void startMatch(Dev& h, Dev& g) {
    ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
    click(h, Button::Confirm);
    ASSERT_TRUE(run([&] { return h.match && g.match; })) << "host:\n" << screenText(h) << "guest:\n" << screenText(g);
  }

  // Taps cell 1 of a match's board (the canvas sits at (3, 6)).
  void tapCellOne(Dev& d) {
    d.input->tap(3 + 27 + 70, 6 + 200 + 70);
    frame(d);
  }

  const nearby::LinkProbe& probeOf(const size_t open) const { return radio->probes[open]; }
};

// ---- the choose screen ----

TEST_F(NearbyLobbyTest, TheChooseScreenOffersHostAndJoinUnderTheGamesNameWithCancelInTheFooter) {
  Dev& d = addDev("a");
  EXPECT_EQ(headerOf(), "Nearby");
  EXPECT_TRUE(shows(d, tr(STR_GAMES_NEARBY_HOST)));
  EXPECT_TRUE(shows(d, tr(STR_GAMES_NEARBY_JOIN)));
  EXPECT_FALSE(shows(d, tr(STR_GAMES_NEARBY_COULD_NOT_START)));
  ASSERT_FALSE(UITheme::getInstance().getTheme().hints.empty());
  const ButtonHints& hints = UITheme::getInstance().getTheme().hints.back();
  EXPECT_EQ(hints.btn1, tr(STR_CANCEL));
  EXPECT_EQ(hints.btn2, tr(STR_SELECT));
  EXPECT_EQ(hints.btn3, tr(STR_DIR_UP));
  EXPECT_EQ(hints.btn4, tr(STR_DIR_DOWN));
  EXPECT_EQ(radio->opens, 0) << "nothing opens the radio until Host or Join is picked";
  EXPECT_TRUE(d.lobby->preventAutoSleep());
}

TEST_F(NearbyLobbyTest, ARadioThatWillNotStartLeavesTheChooseScreenWithTheNoticeAndNoSession) {
  radio->failOpen = true;
  Dev& d = addDev("a");
  ASSERT_TRUE(tapRow(d, tr(STR_GAMES_NEARBY_HOST)));
  EXPECT_EQ(radio->opens, 1);
  EXPECT_TRUE(shows(d, tr(STR_GAMES_NEARBY_COULD_NOT_START))) << screenText(d);
  EXPECT_TRUE(shows(d, tr(STR_GAMES_NEARBY_HOST))) << "the rows are still there";
  EXPECT_TRUE(logHas("the radio did not start"));
  ASSERT_TRUE(tapRow(d, tr(STR_GAMES_NEARBY_JOIN)));
  EXPECT_EQ(radio->opens, 2);
  EXPECT_TRUE(shows(d, tr(STR_GAMES_NEARBY_COULD_NOT_START)));
  EXPECT_TRUE(radio->links.empty()) << "no link was made, so none is left open";
  // A later try succeeds, and the notice goes.
  radio->failOpen = false;
  ASSERT_TRUE(tapRow(d, tr(STR_GAMES_NEARBY_HOST)));
  ASSERT_TRUE(run([&] { return shows(d, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)); }));
  EXPECT_FALSE(shows(d, tr(STR_GAMES_NEARBY_COULD_NOT_START)));
}

// ---- the host ----

TEST_F(NearbyLobbyTest, TheHostScreenShowsItsLabelWaitingOnePlayerAndAGreyedStart) {
  Dev& h = addDev("host");
  ASSERT_NO_FATAL_FAILURE(host(h));
  EXPECT_TRUE(shows(h, labelOf(0))) << screenText(h);
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)));
  EXPECT_TRUE(shows(h, seatsLine(1, 2)));
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_START)));
  EXPECT_TRUE(shows(h, tr(STR_CANCEL)));
  EXPECT_EQ(headerOf(), "Nearby");
  // Start is greyed: Confirm on it starts nothing (the selection starts on Cancel, with Start above it).
  click(h, Button::NavPrevious);
  click(h, Button::Confirm);
  idle(5);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.popped, 0);
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)));
}

TEST_F(NearbyLobbyTest, AGuestThatJoinsEnablesStartAndTheLinesFollowJoinedLeftJoined) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  EXPECT_TRUE(shows(h, seatsLine(2, 2)));
  EXPECT_FALSE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)));
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START)));
  EXPECT_TRUE(shows(g, labelOf(0))) << "the guest names the host it waits for";
  EXPECT_TRUE(shows(g, tr(STR_CANCEL)));
  // The guest cancels before Start: Player 2 left, Start greyed, and the host waits for a player.
  click(g, Button::Back);
  EXPECT_EQ(activityManager.asks.popped, 1);
  ASSERT_TRUE(run([&] { return shows(h, leftLine(2)); })) << screenText(h);
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)));
  EXPECT_FALSE(shows(h, joinedLine(2)));
  click(h, Button::NavPrevious);
  click(h, Button::Confirm);
  idle(5);
  EXPECT_EQ(activityManager.asks.replaced, 0) << "Start is greyed with no guest";
  // A new guest can join, and the line returns to Player 2 joined.
  Dev& g2 = addDev("guest2");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g2));
  ASSERT_NO_FATAL_FAILURE(joinHost(g2, labelOf(0), h));
  EXPECT_TRUE(shows(h, seatsLine(2, 2)));
  EXPECT_FALSE(shows(h, leftLine(2)));
}

TEST_F(NearbyLobbyTest, AGuestNotHeardForTheLiveWindowGreysStartAndHeardAgainEnablesItWhileTheLinesStay) {
  radio->settings = nearby::fastSettings();
  radio->settings.link.silenceMs = 10000;  // the device's limits: the seat outlives the 3 s window
  radio->settings.link.keepaliveMs = 1000;
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  // Heard every second: the seat and Start stay live well past the 3 s window.
  advance(GameLobbyActivity::LIVE_WINDOW_MS + 1000);
  ASSERT_TRUE(shows(h, joinedLine(2)));
  const std::string before = screenText(h);
  // The guest goes quiet: one-way range, its frames are lost while the host's still reach it.
  radio->net.blackhole(radio->links[1].index());
  advance(GameLobbyActivity::LIVE_WINDOW_MS + 500);
  EXPECT_EQ(screenText(h), before) << "only Start greys: the seat is held and the lines stay";
  click(h, Button::NavPrevious);  // the selection moved off the greyed Start to Cancel; this brings it back onto Start
  click(h, Button::Confirm);
  EXPECT_FALSE(run([&] { return h.match != nullptr; }, 400)) << "a guest not heard for 3 s: Start does nothing";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(shows(h, joinedLine(2)));
  // Heard again: Start is enabled (and selected) again, and starts the match.
  radio->net.blackhole(radio->links[1].index(), false);
  advance(1500);
  click(h, Button::Confirm);
  EXPECT_TRUE(run([&] { return h.match && g.match; })) << "heard again, Start starts the match";
}

TEST_F(NearbyLobbyTest, AStartTappedOnADrawnEnabledStartReadsTheGuestsLivenessAgainAndStartsNothingOverAGhost) {
  radio->settings = nearby::fastSettings();
  radio->settings.link.silenceMs = 10000;
  radio->settings.link.keepaliveMs = 1000;
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  advance(1500);
  ASSERT_TRUE(shows(h, joinedLine(2)));
  // Start is drawn enabled from a poll that ran an instant ago, so the tap's own pass does not poll (under POLL_MS
  // since the last), and the guest's last frame is far older than the live window by the time the tap reads the clock:
  // the lobby's first clock read of the pass (the poll check) is the plain one, its second (Start's re-read) is
  // LIVE_WINDOW_MS + 2000 ms later. Without the re-read the stale Start would start a match over a ghost seat.
  frame(h);
  radio->lobbyClock.jumpOnce(1, GameLobbyActivity::LIVE_WINDOW_MS + 2000);
  click(h, Button::Confirm);
  ASSERT_TRUE(radio->lobbyClock.spent()) << "the staged late reading was taken by the lobby's pass";
  EXPECT_FALSE(run([&] { return h.match != nullptr; }, 400)) << "a guest not heard for the live window: Start refuses";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("found the guest quiet"));
  EXPECT_TRUE(shows(h, joinedLine(2))) << "the seat is held: only Start greys";
  // The guest is heard on the plain clock: the next Start goes through.
  advance(1500);
  click(h, Button::Confirm);
  EXPECT_TRUE(run([&] { return h.match && g.match; })) << "heard lately, Start starts the match";
}

TEST_F(NearbyLobbyTest, AGuestThatStaysQuietLosesItsSeatAtTheSilenceLimit) {
  radio->settings = nearby::fastSettings();
  radio->settings.link.silenceMs = 10000;
  radio->settings.link.keepaliveMs = 1000;
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  radio->net.blackhole(radio->links[1].index());
  advance(GameLobbyActivity::LIVE_WINDOW_MS + 500);
  EXPECT_TRUE(shows(h, joinedLine(2))) << "the seat is held until the silence limit";
  ASSERT_TRUE(run([&] { return shows(h, leftLine(2)); }, 20000)) << screenText(h);
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)));
}

// ---- the guest ----

TEST_F(NearbyLobbyTest, TheGuestListsAHostWithItsLabelAndSeatsAndTheEmptyListSaysItIsLooking) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_SEARCHING)));
  EXPECT_TRUE(shows(g, tr(STR_CANCEL)));
  ASSERT_NO_FATAL_FAILURE(host(h));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(1)); })) << screenText(g);
  EXPECT_TRUE(shows(g, seatsLine(1, 2)));
  EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_SEARCHING))) << "the looking line goes once a host is listed";
  EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_DIFFERENT_VERSION)));
  EXPECT_EQ(headerOf(), "Nearby");
}

TEST_F(NearbyLobbyTest, AHostOfAnotherPackageVersionIsListedGreyedAndTakesNoTap) {
  Dev& g = addDev("guest");
  Dev& h = addDev("host", HASH_B);
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  ASSERT_NO_FATAL_FAILURE(host(h));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(1)); })) << screenText(g);
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_DIFFERENT_VERSION))) << screenText(g);
  EXPECT_TRUE(shows(g, seatsLine(1, 2)));
  // A touch on the row and a Confirm on it join nothing.
  ASSERT_TRUE(tapRow(g, labelOf(1)));
  click(g, Button::Confirm);
  idle(20);
  EXPECT_FALSE(logHas("was not sent")) << "a greyed row never reaches the join call";
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER))) << "the host never saw a JOIN";
  EXPECT_TRUE(shows(g, labelOf(1))) << "the guest is still browsing";
  EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START)));
}

TEST_F(NearbyLobbyTest, TheSelectionFollowsTheHostItWasOnWhenAnEarlierHostLeavesTheList) {
  Dev& g = addDev("guest");
  Dev& h1 = addDev("host one");
  Dev& h2 = addDev("host two");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  ASSERT_NO_FATAL_FAILURE(host(h1));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(1)); })) << screenText(g);
  ASSERT_NO_FATAL_FAILURE(host(h2));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(2)); })) << screenText(g);
  click(g, Button::NavNext);  // the second host's row
  // The first host leaves: the second moves up a row, and the selection moves with it, not to what is now at row 1.
  click(h1, Button::Back);
  ASSERT_TRUE(run([&] { return !shows(g, labelOf(1)) && shows(g, labelOf(2)); }, 30000)) << screenText(g);
  click(g, Button::Confirm);
  EXPECT_TRUE(run([&] { return shows(h2, joinedLine(2)); })) << "Confirm joined the host that was selected\n"
                                                             << screenText(g);
  EXPECT_EQ(activityManager.asks.popped, 1) << "only the first host's own Back popped; the guest did not leave";
}

TEST_F(NearbyLobbyTest, DumpsEachLobbyScreenForReviewWhenAsked) {
  if (std::getenv("LOBBY_SHOTS_DIR") == nullptr) GTEST_SKIP() << "LOBBY_SHOTS_DIR is not set";
  Dev& h = addDev("host");
  dumpScreen("1-choose", h.target->drawn);
  ASSERT_NO_FATAL_FAILURE(host(h));
  dumpScreen("2-host-waiting", h.target->drawn);
  Dev& other = addDev("other", HASH_B);
  ASSERT_NO_FATAL_FAILURE(host(other));
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(0)) && shows(g, labelOf(1)); })) << screenText(g);
  dumpScreen("5-guest-browse", g.target->drawn);
  ASSERT_NO_FATAL_FAILURE(joinHost(g, labelOf(0), h));
  dumpScreen("3-host-player-2-joined", h.target->drawn);
  dumpScreen("6-guest-waiting-for-host", g.target->drawn);
  Dev& late = addDev("late");
  ASSERT_NO_FATAL_FAILURE(joinScreen(late));
  ASSERT_TRUE(run([&] { return shows(late, labelOf(0)) && shows(late, seatsLine(2, 2)); })) << screenText(late);
  ASSERT_TRUE(tapRow(late, labelOf(0)));
  ASSERT_TRUE(run([&] { return shows(late, tr(STR_GAMES_NEARBY_GAME_FULL)); })) << screenText(late);
  dumpScreen("8-guest-game-full", late.target->drawn);
  click(g, Button::Back);
  ASSERT_TRUE(run([&] { return shows(h, leftLine(2)); })) << screenText(h);
  dumpScreen("4-host-player-2-left", h.target->drawn);
  Dev& again = addDev("again");
  ASSERT_NO_FATAL_FAILURE(joinScreen(again));
  ASSERT_NO_FATAL_FAILURE(joinHost(again, labelOf(0), h));
  click(h, Button::Back);
  ASSERT_TRUE(run([&] { return shows(again, tr(STR_GAMES_NEARBY_HOST_LEFT)); })) << screenText(again);
  dumpScreen("7-guest-host-left", again.target->drawn);
}

TEST_F(NearbyLobbyTest, ATapOnRowsRebuiltSinceTheyWereDrawnIsDroppedAndNeitherJoinsNorLeaves) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(host(h));
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(0)); }));
  // The host goes while the guest's loop is not running (a long refresh): the guest's session expires it, but the panel
  // still shows its row, and the touch table is the old one.
  click(h, Button::Back);
  const uint64_t until = fakertos::millis() + 2 * GameLobbyActivity::LIST_REFRESH_MIN_MS + 4000;
  nearby::waitFor([&] { return fakertos::millis() >= until; }, 60000);
  ASSERT_TRUE(shows(g, labelOf(0))) << "the panel was not redrawn";
  // The tap's own pass reads the session first, so the rows are rebuilt to just Cancel before the tap is dispatched;
  // the old table's first row is now Cancel's index. It must not leave the lobby.
  ASSERT_TRUE(tapRow(g, labelOf(0)));
  EXPECT_EQ(activityManager.asks.popped, 1) << "only the host's own Back popped";
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_SEARCHING))) << "the guest redrew the rows it has now\n" << screenText(g);
  EXPECT_FALSE(shows(g, labelOf(0)));
  // The rows drawn now are the ones to tap: Cancel leaves.
  ASSERT_TRUE(tapRow(g, tr(STR_CANCEL)));
  EXPECT_EQ(activityManager.asks.popped, 2);
}

// A raw host (an oracle that speaks Protocol by hand) on the radio's net, advertising this game.
struct RawHost {
  explicit RawHost(FakeNearbyRadio& radio)
      : peer(radio.net, rawClock), game(GameLobbyActivity::lobbyGameFor(nearbyManifest(), HASH_A)) {}
  void advertise() { ASSERT_TRUE(peer.broadcast(GameCore::LobbyTest::advertFrameFor(game, 1, 2, label), RAW_SESSION)); }
  GameCore::LinkTest::ManualClock rawClock;
  GameCore::LobbyTest::RawPeer peer;
  GameCore::LobbyGame game;
  GameCore::LobbyTest::Bytes label = GameCore::LobbyTest::bytesOf("Raw host");
};

TEST_F(NearbyLobbyTest, AJoinTheHostStillRefusesShowsDifferentVersionOrFullAndBackToTheListListsHostsAgain) {
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  RawHost raw(*radio);
  raw.peer.connect(radio->links[0]);
  for (const auto reason : {GameCore::Protocol::AbortReason::VersionMismatch, GameCore::Protocol::AbortReason::Full}) {
    ASSERT_TRUE(run([&] {
      raw.advertise();
      return shows(g, "Raw host");
    })) << screenText(g);
    ASSERT_TRUE(tapRow(g, "Raw host"));
    ASSERT_TRUE(run([&] {
      raw.peer.drain();
      return raw.peer.count(GameCore::Protocol::Type::Join) >= 1;
    }));
    ASSERT_TRUE(raw.peer.put(GameCore::LobbyTest::abortFrame(reason), RAW_SESSION));
    const std::string said = reason == GameCore::Protocol::AbortReason::Full ? tr(STR_GAMES_NEARBY_GAME_FULL)
                                                                             : tr(STR_GAMES_NEARBY_DIFFERENT_VERSION);
    ASSERT_TRUE(run([&] { return shows(g, said); })) << screenText(g);
    EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_BACK_TO_LIST)));
    EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START)));
    // Back to the list returns to the list: the host is listed again once it advertises.
    ASSERT_TRUE(tapRow(g, tr(STR_GAMES_NEARBY_BACK_TO_LIST)));
    ASSERT_TRUE(run([&] { return shows(g, tr(STR_GAMES_NEARBY_SEARCHING)); })) << screenText(g);
    raw.peer.seen.clear();
  }
  EXPECT_EQ(activityManager.asks.popped, 0) << "Back to the list leaves nothing";
}

TEST_F(NearbyLobbyTest, AThirdGuestIsToldTheGameIsFull) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  Dev& g2 = addDev("late");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g2));
  ASSERT_TRUE(run([&] { return shows(g2, labelOf(0)) && shows(g2, seatsLine(2, 2)); })) << screenText(g2);
  ASSERT_TRUE(tapRow(g2, labelOf(0)));
  ASSERT_TRUE(run([&] { return shows(g2, tr(STR_GAMES_NEARBY_GAME_FULL)); })) << screenText(g2);
  EXPECT_TRUE(shows(g2, tr(STR_GAMES_NEARBY_BACK_TO_LIST)));
  EXPECT_TRUE(shows(h, joinedLine(2))) << "the seated guest is not disturbed";
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START)));
}

TEST_F(NearbyLobbyTest, WhenTheHostLeavesTheGuestSaysSoAndBackToTheListListsTheHostsThere) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  click(h, Button::Back);
  ASSERT_TRUE(run([&] { return shows(g, tr(STR_GAMES_NEARBY_HOST_LEFT)); })) << screenText(g);
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_BACK_TO_LIST)));
  EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START)));
  // Another host starts hosting: Back to the list lists it.
  Dev& h2 = addDev("host2");
  ASSERT_NO_FATAL_FAILURE(host(h2));
  click(g, Button::Confirm);  // the one row, selected
  ASSERT_TRUE(run([&] { return shows(g, labelOf(2)); })) << screenText(g);
  EXPECT_FALSE(shows(g, tr(STR_GAMES_NEARBY_HOST_LEFT)));
  EXPECT_EQ(activityManager.asks.popped, 1) << "only the host's Back popped; Back to the list leaves nothing";
}

TEST_F(NearbyLobbyTest, AHostThatGoesSilentEndsTheGuestsWaitWithTheHostLeftScreen) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  radio->net.blackhole(radio->links[0].index());  // nothing the host sends is heard any more
  ASSERT_TRUE(run([&] { return shows(g, tr(STR_GAMES_NEARBY_HOST_LEFT)); }, 20000)) << screenText(g);
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_BACK_TO_LIST)));
}

// ---- starting the match ----

TEST_F(NearbyLobbyTest, StartReplacesBothLobbiesWithTheMatchTheSessionMovedInAndTheMatchPlays) {
  const size_t opsBefore = fakesd::sim().ops.size();
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(startMatch(h, g));
  EXPECT_EQ(activityManager.asks.replaced, 2) << "one replacement for each device";
  EXPECT_EQ(h.lobby, nullptr);
  EXPECT_EQ(g.lobby, nullptr);
  EXPECT_TRUE(h.lobbyExited);
  EXPECT_TRUE(g.lobbyExited);
  EXPECT_TRUE(h.match->preventAutoSleep()) << "the match holds the session: a nearby match keeps the device awake";
  EXPECT_TRUE(g.match->preventAutoSleep());
  EXPECT_EQ(probeOf(0).endCalls.load(), 0) << "the lobby's exit did not end the link: the match owns it";
  EXPECT_EQ(probeOf(1).endCalls.load(), 0);
  EXPECT_EQ(probeOf(0).abortFrames.load(), 0);
  EXPECT_EQ(probeOf(1).abortFrames.load(), 0);
  ASSERT_TRUE(run([&] { return shows(h, "Player 1 (X) to move") && shows(g, "Player 2 (O): Player 1 to move"); }))
      << "host:\n"
      << screenText(h) << "guest:\n"
      << screenText(g);
  EXPECT_EQ(fakelog::countLines("setup mode nearby seats 2"), 1u) << "only the host runs setup";
  // The match plays: the host takes cell 1 and the guest's turn follows.
  tapCellOne(h);
  ASSERT_TRUE(run([&] { return shows(g, "Player 2 (O) to move"); })) << screenText(g);
  for (size_t i = opsBefore; i < fakesd::sim().ops.size(); ++i) {
    EXPECT_EQ(fakesd::sim().ops[i].find("resume.bin"), std::string::npos)
        << "a nearby match and its lobby touch no resume.bin: " << fakesd::sim().ops[i];
  }
  EXPECT_EQ(fakelock::taskAcquisitions.load(), 0) << "a task thread took the RenderLock";
  EXPECT_EQ(fakelock::taskAttempts.load(), 0) << "a task thread tried the RenderLock";
}

TEST_F(NearbyLobbyTest, TheTitleScreensChosenSettingsReachTheMatch) {
  GameCore::SettingValues settings;
  std::snprintf(settings.entries[0].id, sizeof(settings.entries[0].id), "level");
  std::snprintf(settings.entries[0].value, sizeof(settings.entries[0].value), "Hard");
  settings.count = 1;
  Dev& h = addDev("host", HASH_A, settings);
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(startMatch(h, g));
  // The nearby fixture ignores settings; what is pinned here is that the host's match was built (setup ran once).
  ASSERT_TRUE(run([&] { return fakelog::countLines("setup mode nearby seats 2") == 1u; }));
}

TEST_F(NearbyLobbyTest, AMatchThatCannotBeAllocatedGoesBackToChooseWithTheNoticeAndEndsBothSessions) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  oom::failSize = sizeof(GameMatchActivity);
  click(h, Button::Confirm);
  ASSERT_TRUE(run([&] {
    return shows(h, tr(STR_GAMES_NEARBY_COULD_NOT_START)) && shows(g, tr(STR_GAMES_NEARBY_COULD_NOT_START));
  })) << "host:\n"
      << screenText(h) << "guest:\n"
      << screenText(g);
  oom::failSize = 0;
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(h.match == nullptr);
  EXPECT_TRUE(logHas("OOM:"));
  EXPECT_EQ(probeOf(0).endCalls.load(), 1) << "the host's link was ended once";
  EXPECT_EQ(probeOf(1).endCalls.load(), 1) << "and the guest's";
  EXPECT_GE(probeOf(0).abortFrames.load(), 1) << "the host said goodbye to the guest it had started";
  EXPECT_TRUE(shows(h, tr(STR_GAMES_NEARBY_HOST))) << "back on the choose screen";
}

// ---- Cancel, Back, Home, a forced exit ----

TEST_F(NearbyLobbyTest, CancelOnAHostWithAGuestSendsAbortLeftThriceEndsTheLinkOnceAndPopsToTheTitleScreen) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  ASSERT_TRUE(tapRow(h, tr(STR_CANCEL)));
  EXPECT_EQ(activityManager.asks.popped, 1);
  EXPECT_EQ(probeOf(0).endCalls.load(), 1);
  EXPECT_EQ(probeOf(0).abortFrames.load(), static_cast<int>(GameCore::CANCEL_ABORT_COPIES));
  ASSERT_TRUE(run([&] { return shows(g, tr(STR_GAMES_NEARBY_HOST_LEFT)); })) << screenText(g);
  // The manager's exit and destruction after the pop send and end nothing again.
  activityManager.exitHolding(*h.lobby);
  h.lobbyExited = true;
  activityManager.destroyHolding(h.lobby);
  EXPECT_EQ(probeOf(0).endCalls.load(), 1);
  EXPECT_EQ(probeOf(0).abortFrames.load(), static_cast<int>(GameCore::CANCEL_ABORT_COPIES));
}

TEST_F(NearbyLobbyTest, BackOnASeatedGuestSendsAbortLeftThriceAndEndsItsLinkOnce) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
  click(g, Button::Back);
  EXPECT_EQ(activityManager.asks.popped, 1);
  EXPECT_EQ(probeOf(1).endCalls.load(), 1);
  EXPECT_EQ(probeOf(1).abortFrames.load(), static_cast<int>(GameCore::CANCEL_ABORT_COPIES));
  ASSERT_TRUE(run([&] { return shows(h, leftLine(2)); })) << screenText(h);
}

TEST_F(NearbyLobbyTest, AHostWithNoGuestCancelsWithoutAnyAbortAndStillEndsTheLink) {
  Dev& h = addDev("host");
  ASSERT_NO_FATAL_FAILURE(host(h));
  click(h, Button::Back);
  EXPECT_EQ(activityManager.asks.popped, 1);
  EXPECT_EQ(probeOf(0).endCalls.load(), 1);
  EXPECT_EQ(probeOf(0).abortFrames.load(), 0) << "nobody was seated: nobody to say goodbye to";
}

TEST_F(NearbyLobbyTest, AForcedExitOnAnyScreenTearsDownOnceAndTakesNoRenderLock) {
  // Home or sleep: the manager exits the screen with the render mutex held; the lobby takes none (RenderLock would
  // deadlock: ScreenTest::TearDown fails on selfDeadlocks).
  {
    Dev& d = addDev("choose");
    activityManager.exitHolding(*d.lobby);
    d.lobbyExited = true;
    EXPECT_EQ(radio->opens, 0);
  }
  {
    Dev& h = addDev("host");
    Dev& g = addDev("guest");
    ASSERT_NO_FATAL_FAILURE(hostAndJoin(h, g));
    // The seated guest first: ABORT(left) thrice to the host, the link ended once.
    activityManager.exitHolding(*g.lobby);
    g.lobbyExited = true;
    EXPECT_EQ(probeOf(1).endCalls.load(), 1);
    EXPECT_EQ(probeOf(1).abortFrames.load(), static_cast<int>(GameCore::CANCEL_ABORT_COPIES));
    activityManager.destroyHolding(g.lobby);
    EXPECT_EQ(probeOf(1).endCalls.load(), 1) << "the destructor ends nothing again";
    ASSERT_TRUE(run([&] { return shows(h, leftLine(2)); }));
    // The host, now alone: no goodbye, one end.
    activityManager.exitHolding(*h.lobby);
    h.lobbyExited = true;
    EXPECT_EQ(probeOf(0).endCalls.load(), 1);
    EXPECT_EQ(probeOf(0).abortFrames.load(), 0);
  }
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
}

TEST_F(NearbyLobbyTest, AForcedExitWhileBrowsingOrAfterTheHostLeftEndsTheLinkOnceWithNoAbort) {
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  activityManager.exitHolding(*g.lobby);
  g.lobbyExited = true;
  EXPECT_EQ(probeOf(0).endCalls.load(), 1);
  EXPECT_EQ(probeOf(0).abortFrames.load(), 0);
  // Host left: the guest's ended screen, then Back.
  Dev& h = addDev("host");
  Dev& g2 = addDev("guest2");
  ASSERT_NO_FATAL_FAILURE(host(h));
  ASSERT_NO_FATAL_FAILURE(joinScreen(g2));
  ASSERT_NO_FATAL_FAILURE(joinHost(g2, labelOf(1), h));
  click(h, Button::Back);
  ASSERT_TRUE(run([&] { return shows(g2, tr(STR_GAMES_NEARBY_HOST_LEFT)); }));
  const int aborts = probeOf(2).abortFrames.load();
  click(g2, Button::Back);
  EXPECT_EQ(activityManager.asks.popped, 2);
  EXPECT_EQ(probeOf(2).endCalls.load(), 1);
  EXPECT_EQ(probeOf(2).abortFrames.load(), aborts) << "the guest was no longer seated: it says nothing more";
}

// ---- how often a screen redraws (D2) ----

TEST_F(NearbyLobbyTest, AStableListOfHostsCostsNoRefreshAndListChangesAreSpacedByTheBound) {
  Dev& g = addDev("guest");
  std::vector<uint64_t> pushedAt;
  g.renderer->onDisplay = [&] { pushedAt.push_back(fakertos::millis()); };
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  Dev& h1 = addDev("host1");
  ASSERT_NO_FATAL_FAILURE(host(h1));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(1)); }));
  // A stable host: its ADVERTs every 500 ms refresh nothing on the guest's panel.
  const size_t stable = pushes(g);
  advance(4000);
  EXPECT_EQ(pushes(g), stable) << "nothing changed, so nothing was drawn";
  // Two more hosts appear close together: their rows come with one refresh, not one each.
  Dev& h2 = addDev("host2");
  Dev& h3 = addDev("host3");
  ASSERT_NO_FATAL_FAILURE(host(h2));
  ASSERT_NO_FATAL_FAILURE(host(h3));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(2)) && shows(g, labelOf(3)); })) << screenText(g);
  for (size_t i = 1; i < pushedAt.size(); ++i) {
    EXPECT_GE(pushedAt[i] - pushedAt[i - 1], GameLobbyActivity::LIST_REFRESH_MIN_MS)
        << "push " << i << " came too soon after the one before it";
  }
}

TEST_F(NearbyLobbyTest, AJoinIsDrawnAtOnceWithoutWaitingForTheRefreshBound) {
  Dev& h = addDev("host");
  Dev& g = addDev("guest");
  ASSERT_NO_FATAL_FAILURE(host(h));
  ASSERT_NO_FATAL_FAILURE(joinScreen(g));
  ASSERT_TRUE(run([&] { return shows(g, labelOf(0)); }));
  const uint64_t tapped = fakertos::millis();
  ASSERT_TRUE(tapRow(g, labelOf(0)));
  EXPECT_TRUE(shows(g, tr(STR_GAMES_NEARBY_WAITING_FOR_START))) << "drawn by the tap's own pass";
  EXPECT_LT(fakertos::millis() - tapped, GameLobbyActivity::LIST_REFRESH_MIN_MS);
}

// ---- the radio double, and what the lobby takes from the radio ----

TEST_F(NearbyLobbyTest, TheFakeRadioHandsOutWhatTheDeviceRadioDoes) {
  // The label is EspNow::makeLabel's form and fits an ADVERT.
  NearbyRadio::Opened first = radio->open();
  NearbyRadio::Opened second = radio->open();
  ASSERT_TRUE(first.link);
  ASSERT_TRUE(second.link);
  EXPECT_LE(first.labelLength, GameCore::Protocol::MAX_HOST_LABEL_BYTES);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(first.label), first.labelLength), labelOf(0));
  EXPECT_EQ(labelOf(0), "X4 Pro 3F2A");
  EXPECT_NE(labelOf(0), labelOf(1));
  // Every open() is an independent link: ending one leaves the other sending.
  const std::vector<uint8_t> frame = {1};
  first.link->end();
  EXPECT_FALSE(first.link->broadcast(frame));
  EXPECT_TRUE(second.link->broadcast(frame));
  // The device's own radio asks makeEspNowLink() for its link (entry 11): the harness's double is a host with no radio,
  // so its bring-up fails and the radio starts no link, as on a device whose radio would not start. It also reads the
  // probe and keeps the lobby timing defaults.
  NearbyRadio device;
  espnowlink::reset();
  const NearbyRadio::Opened none = device.open();
  EXPECT_EQ(none.link, nullptr);
  EXPECT_EQ(none.labelLength, 0u);
  EXPECT_EQ(espnowlink::script().attempts, 1) << "one open(), one bring-up";
  // The success path of the same open(): a radio that comes up gives the link and the label the factory built from
  // the MAC (EspNow::makeLabel, "X4 Pro" and the last two bytes), copied whole into Opened.
  espnowlink::reset();
  espnowlink::script().succeed = true;
  {
    NearbyRadio::Opened up = device.open();
    ASSERT_NE(up.link, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(up.label), up.labelLength), "X4 Pro 3F2A");
    EXPECT_LE(up.labelLength, GameCore::Protocol::MAX_HOST_LABEL_BYTES);
    EXPECT_EQ(espnowlink::script().attempts, 1);
    EXPECT_EQ(espnowlink::script().shutdowns, 0) << "up, not torn down yet";
    up.link.reset();  // whoever owns the link ends it: the radio goes off
    EXPECT_EQ(espnowlink::script().shutdowns, 1);
    EXPECT_EQ(espnowlink::script().stationsLeft, 1) << "Wi-Fi handed back";
  }
  espnowlink::reset();
  nearbyprobe::script().wifiInUse = true;
  nearbyprobe::script().freeInternalHeapBytes = 1234;
  const NearbyProbe::Readings read = device.probe();
  EXPECT_TRUE(read.wifiInUse);
  EXPECT_EQ(read.freeInternalHeapBytes, 1234u);
  EXPECT_EQ(nearbyprobe::script().reads, 1);
  const GameCore::LobbySettings defaults = device.lobbySettings();
  EXPECT_EQ(defaults.advertMs, 500u);
  EXPECT_EQ(defaults.hostExpireMs, 3000u);
  EXPECT_EQ(defaults.advertGraceMs, 1000u);
  EXPECT_EQ(defaults.refusalAnswerMs, 200u);
  EXPECT_EQ(defaults.link.resendMs, 400u);
  EXPECT_EQ(defaults.link.silenceMs, 10000u);
  EXPECT_EQ(defaults.link.keepaliveMs, 1000u);
  EXPECT_EQ(device.sessionOptions().stepMs, GameLink::STEP_MS);
  EXPECT_EQ(device.sessionOptions().joinMs, GameLink::JOIN_MS);
  const uint64_t a = device.clock().nowMs();
  const uint64_t b = device.clock().nowMs();
  EXPECT_LE(a, b) << "a monotonic clock";
  // The probe the title screen asks is the installed radio's.
  radio->readings = NearbyProbe::Readings{true, 77};
  EXPECT_TRUE(GameLobbyActivity::probe().wifiInUse);
  EXPECT_EQ(radio->probeReads, 1);
  GameLobbyActivity::setRadio(nullptr);
  EXPECT_EQ(GameLobbyActivity::probe().freeInternalHeapBytes, 1234u) << "with none set, the device's";
  GameLobbyActivity::setRadio(radio.get());
}

TEST_F(NearbyLobbyTest, TheLobbySpeaksForTheGameOnThisHost) {
  const Manifest manifest = nearbyManifest();
  const GameCore::LobbyGame game = GameLobbyActivity::lobbyGameFor(manifest, HASH_A);
  EXPECT_STREQ(game.gameId, "nearby");
  EXPECT_TRUE(std::equal(game.pkgHash.begin(), game.pkgHash.end(), HASH_A));
  EXPECT_EQ(static_cast<int>(game.manifestApi), manifest.api);
  EXPECT_EQ(game.seatsMin, 2);
  EXPECT_EQ(game.seatsMax, 2);
  EXPECT_EQ(game.surface, static_cast<uint32_t>(API_SURFACE_CRC));
  EXPECT_EQ(game.apiPreview, !API_LEVEL_FROZEN);
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(game.caps.api, caps.api);
  EXPECT_EQ(game.caps.maxSeats, caps.maxSeats);
  EXPECT_EQ(game.caps.nearby, caps.nearby);
  EXPECT_TRUE(game.caps.nearby) << "the scripted host has nearby on";
  EXPECT_EQ(static_cast<int>(game.seatLimit()), 2);
  EXPECT_EQ(static_cast<int>(game.startSeats()), 2);
}

// ---- the title screen's Nearby start ----

class NearbyTitleTest : public LobbyRigTest {
 protected:
  void SetUp() override {
    LobbyRigTest::SetUp();
    fakesd::addFile("/.games/nearby/.pkg", launcher::PKG);
    ButtonNavigator::setMappedInputManager(*input);
  }
  void TearDown() override {
    for (auto pushed = activityManager.pushedActivities.rbegin(); pushed != activityManager.pushedActivities.rend();
         ++pushed) {
      if (pushed->get() == title) activityManager.exitHolding(**pushed);
      activityManager.destroyHolding(*pushed);
    }
    activityManager.pushedActivities.clear();
    title = nullptr;
    LobbyRigTest::TearDown();
  }

  // The title screen of the nearby fixture, entered and drawn; `defaultMode` is the manifest's default_mode.
  void openTitle(const uint8_t modes = PASS | NEARBY, const uint8_t defaultMode = 0, const uint8_t settingsCount = 0) {
    GameRegistry::Entry game;
    game.manifest = nearbyManifest();
    game.manifest.defaultMode = defaultMode;
    game.manifest.settingsCount = settingsCount;
    game.check = GameCore::CheckResult{GameCore::CheckStatus::Ok, GameCore::CheckReason::None, modes};
    std::copy(HASH_A, HASH_A + GamePkg::HASH_BYTES, game.pkgHash);
    auto made = std::make_unique<GameModeActivity>(*renderer, *input, game);
    title = made.get();
    activityManager.pushedActivities.push_back(std::move(made));
    static_cast<Activity*>(title)->onEnter();  // through the base: the title screen's own onEnter is private
    render();
  }
  void render() {
    ui().forget();
    title->render(RenderLock(*title));
    activityManager.markRendered();
  }
  void frame() {
    title->loop();
    input->clear();
  }
  screen::RecordingTarget& ui() { return launcher::TargetOf::of(*title); }
  bool drew(const std::string& line) { return ui().drewLine(line); }
  std::string lineUnder(const std::string& label) {
    const std::vector<screen::DrawnText>& drawn = ui().drawn;
    for (size_t i = 0; i + 1 < drawn.size(); ++i)
      if (drawn[i].text == label) return drawn[i + 1].text;
    return "";
  }
  void tapRow(const std::string& label) {
    for (const screen::DrawnText& drawn : ui().drawn) {
      if (drawn.text != label) continue;
      input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      frame();
      return;
    }
    FAIL() << "the screen does not draw \"" << label << "\": " << ui().joined();
  }
  void click(const Button button) {
    input->click(button);
    frame();
  }
  static void remember(const uint8_t mode) {
    GameSaveStore::Prefs prefs;
    prefs.mode = mode;
    ASSERT_TRUE(GameSaveStore::savePrefs("nearby", prefs));
  }
  static uint8_t rememberedMode() {
    GameSaveStore::Prefs prefs;
    GameSaveStore::loadPrefs("nearby", prefs);
    return prefs.mode;
  }
  // The title screen shows the refusal: its dialog is up and the list is not. The message is read as the screen passed
  // it (textCalls): the double wraps what it draws.
  bool said(const char* message) {
    for (const screen::DrawnText& call : ui().textCalls)
      if (call.text == message) return true;
    return false;
  }
  bool dialogUp(const char* message) {
    return drew(tr(STR_GAMES_MODE_NEARBY)) && said(message) && drew(tr(STR_OK_BUTTON)) && !drew(tr(STR_GAMES_NEW_GAME));
  }
  bool lobbyPushed() {
    return activityManager.pushedActivities.size() == 2 &&
           dynamic_cast<GameLobbyActivity*>(activityManager.pushedActivities.back().get()) != nullptr;
  }

  GameModeActivity* title = nullptr;
};

TEST_F(NearbyTitleTest, DumpsTheRefusalDialogsForReviewWhenAsked) {
  if (std::getenv("LOBBY_SHOTS_DIR") == nullptr) GTEST_SKIP() << "LOBBY_SHOTS_DIR is not set";
  remember(NEARBY);
  radio->readings = NearbyProbe::Readings{true, SIZE_MAX};
  openTitle();
  dumpScreen("0-title-nearby", ui().drawn);
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  ASSERT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_WIFI_IN_USE))) << ui().joined();
  dumpScreen("9-refusal-wifi", ui().textCalls);
}

TEST_F(NearbyTitleTest, WithWifiInUseNewGameShowsTheWifiDialogOpensNoRadioAndPushesNoLobby) {
  remember(NEARBY);
  radio->readings = NearbyProbe::Readings{true, SIZE_MAX};
  openTitle();
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_WIFI_IN_USE))) << ui().joined();
  EXPECT_EQ(radio->opens, 0) << "no radio opened";
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u) << "no lobby pushed";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(radio->probeReads, 1);
  EXPECT_EQ(rememberedMode(), NEARBY);
}

TEST_F(NearbyTitleTest, TheDialogClosesOnOkBackOrConfirmAndTheTitleScreenIsAsItWas) {
  remember(NEARBY);
  radio->readings = NearbyProbe::Readings{true, SIZE_MAX};
  openTitle();
  for (int how = 0; how < 3; ++how) {
    tapRow(tr(STR_GAMES_NEW_GAME));
    render();
    ASSERT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_WIFI_IN_USE))) << "how " << how << ": " << ui().joined();
    if (how == 0) tapRow(tr(STR_OK_BUTTON));
    if (how == 1) click(Button::Back);
    if (how == 2) click(Button::Confirm);
    render();
    EXPECT_TRUE(drew(tr(STR_GAMES_NEW_GAME))) << "how " << how << ": the list is back";
    EXPECT_EQ(lineUnder(tr(STR_GAMES_NEW_GAME)), tr(STR_GAMES_MODE_NEARBY)) << "how " << how;
    EXPECT_EQ(activityManager.asks.popped, 0) << "Back closed the dialog, not the title screen";
    EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  }
  EXPECT_EQ(radio->opens, 0);
}

TEST_F(NearbyTitleTest, LowHeapRefusesAtTheBoundaryAndNamesTheRestart) {
  remember(NEARBY);
  openTitle();
  radio->readings = NearbyProbe::Readings{false, 102399};
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_LOW_MEMORY))) << ui().joined();
  EXPECT_FALSE(said(tr(STR_GAMES_NEARBY_WIFI_IN_USE)));
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  click(Button::Back);
  // One byte more passes.
  radio->readings = NearbyProbe::Readings{false, 102400};
  render();
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_TRUE(lobbyPushed());
  EXPECT_EQ(radio->opens, 0) << "the lobby opens the radio when Host or Join is picked, not before";
}

TEST_F(NearbyTitleTest, WithNoRadioSetTheDeviceRadiosProbeIsRead) {
  remember(NEARBY);
  GameLobbyActivity::setRadio(nullptr);
  nearbyprobe::script().freeInternalHeapBytes = 50 * 1024;
  openTitle();
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_LOW_MEMORY))) << ui().joined();
  EXPECT_EQ(nearbyprobe::script().reads, 1);
  EXPECT_EQ(radio->probeReads, 0);
  GameLobbyActivity::setRadio(radio.get());
}

TEST_F(NearbyTitleTest, WithWifiUpAndLowHeapTheWifiDialogShows) {
  remember(NEARBY);
  radio->readings = NearbyProbe::Readings{true, 1000};
  openTitle();
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_TRUE(dialogUp(tr(STR_GAMES_NEARBY_WIFI_IN_USE))) << ui().joined();
  EXPECT_FALSE(said(tr(STR_GAMES_NEARBY_LOW_MEMORY)));
}

TEST_F(NearbyTitleTest, NearbyOverAPassSaveAsksNoQuestionKeepsResumeBinAndRemembersNearby) {
  const savefile::Bytes save = savefile::resumeBytes(savefile::snapshotOf(2), 3, HASH_A, 1, 2);
  fakesd::addFile(resumePath(), save);
  openTitle(PASS | NEARBY, NEARBY);  // nearby is the current mode by the manifest's default, with no prefs.bin
  ASSERT_EQ(lineUnder(tr(STR_GAMES_NEW_GAME)), tr(STR_GAMES_MODE_NEARBY));
  ASSERT_TRUE(drew(tr(STR_GAMES_CONTINUE))) << "the pass save is offered to Continue";
  EXPECT_EQ(rememberedMode(), 0) << "prefs.bin holds nothing yet";
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_FALSE(drew(tr(STR_GAMES_NEW_OVER_SAVE_TITLE))) << "a nearby match replaces nothing: no question";
  EXPECT_TRUE(lobbyPushed());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(fakesd::bytesOf(resumePath()), save) << "resume.bin is byte-identical";
  EXPECT_EQ(rememberedMode(), NEARBY) << "prefs.bin holds Nearby";
}

TEST_F(NearbyTitleTest, ALobbyThatCannotBeAllocatedPushesNothingOpensNoRadioAndWritesNothing) {
  openTitle(PASS | NEARBY, NEARBY);  // nearby by the manifest's default: prefs.bin holds nothing
  oom::failSize = sizeof(GameLobbyActivity);
  tapRow(tr(STR_GAMES_NEW_GAME));
  oom::failSize = 0;
  EXPECT_TRUE(logHas("OOM:"));
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u) << "no lobby";
  EXPECT_EQ(radio->opens, 0);
  EXPECT_EQ(rememberedMode(), 0) << "the mode is remembered when the lobby opens, not before";
  // Not pinned: the repaint after the failure (a tap asks for one by itself on this double, so a test cannot tell).
}

TEST_F(NearbyTitleTest, BackFromTheLobbyLandsOnTheTitleScreenAsItWas) {
  remember(NEARBY);
  openTitle();
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_TRUE(lobbyPushed());
  Activity& pushed = *activityManager.pushedActivities.back();
  pushed.onEnter();
  ButtonNavigator::setMappedInputManager(*input);
  input->click(Button::Back);
  pushed.loop();
  input->clear();
  ASSERT_EQ(activityManager.asks.popped, 1) << "Back finishes the lobby";
  activityManager.popForResult(*title, pushed);
  render();
  EXPECT_TRUE(drew(tr(STR_GAMES_NEW_GAME)));
  EXPECT_EQ(lineUnder(tr(STR_GAMES_NEW_GAME)), tr(STR_GAMES_MODE_NEARBY));
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
}

TEST_F(NearbyTitleTest, UnreadSettingsStartNothingAndReadNoProbe) {
  remember(NEARBY);
  openTitle(PASS | NEARBY, 0, 1);  // the manifest declares one setting the card's manifest.json does not hold
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  EXPECT_EQ(lineUnder(tr(STR_GAMES_NEW_GAME)), tr(STR_GAMES_SETTINGS_NOT_READ));
  EXPECT_EQ(radio->probeReads, 0);
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  EXPECT_EQ(radio->opens, 0);
}

TEST_F(NearbyTitleTest, WithNearbyOffTheTitleScreenStartsPassAsBeforeAndNeverReadsTheProbe) {
  hostcaps::script().nearby = false;
  openTitle(PASS);
  EXPECT_EQ(lineUnder(tr(STR_GAMES_NEW_GAME)), tr(STR_GAMES_MODE_PASS));
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(radio->probeReads, 0);
  EXPECT_EQ(nearbyprobe::script().reads, 0);
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  EXPECT_TRUE(logHas("Mode pass picked"));
  for (auto& replacement : activityManager.replacements) activityManager.destroyHolding(replacement);
  activityManager.replacements.clear();
}

}  // namespace

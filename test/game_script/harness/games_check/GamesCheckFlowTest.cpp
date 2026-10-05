// The pin of RoundPlayer's hidden flow (D2 of the games check's plan) to GameVM's. RoundPlayer stands in for
// GameVM::stepHandOff when it plays a hidden pass round: it shows the turn seat, delivers a step to that seat only,
// shows the mover after a move that passes the turn, then the next seat, and seat 0 once the round is over. This suite
// plays the same four taps of the pass-hidden fixture through both, the real GameVM (its task on host threads, over the
// match's doubles, as GameVmTest does) and RoundPlayer with drawEveryLocalSeat off, and compares what the game saw: the
// order of its `draw`, `tap`, `apply`, and `over` log lines, which name the seat of every call. A change to GameVM's
// hand-off that RoundPlayer does not follow fails here, and the other way round.

#include <GfxRenderer.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ArenaSize.h"
#include "FrameReplay.h"
#include "GameAssets.h"
#include "GameViewport.h"
#include "MatchStore.h"
#include "MatchSupport.h"
#include "RoundFile.h"
#include "RoundPlayer.h"
#include "ScriptVm.h"
#include "TestSupport.h"

namespace {

using match::installFixture;
using match::installGame;
using match::waitFor;

GameCore::GameEvent tapAt(const int x, const int y) {
  GameCore::GameEvent event;
  event.kind = GameCore::EventKind::Tap;
  event.x = static_cast<int16_t>(x);
  event.y = static_cast<int16_t>(y);
  return event;
}

constexpr const char* FOUR_TAPS = R"lua(
return { mode = "pass", steps = {
  { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 }, { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 } },
  winners = {} }
)lua";

// What the match does after a tap of a scenario: nothing (the same seat is still shown, a move that keeps the turn or a
// rejected tap), the turn passes (the mover's frame, then the next seat once the hand-off is done), or the round ends.
enum class After { Stays, Passes, Ends };

// A game on the fake card, the round RoundPlayer plays over it, and what GameVM's flow does after each of its taps.
struct Scenario {
  std::string id;
  std::string round;
  std::vector<After> afters;
  std::vector<std::pair<int, int>> taps;  // canvas x, y of each tap, one per entry of afters
};

class HiddenFlowPinTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    replay.loadFonts(*renderer);
    viewport = GameViewport::forRenderer(*renderer);
  }

  void TearDown() override {
    fakertos::release();
    if (vm && !vm->stop(5000)) {
      ADD_FAILURE() << "the VM would not stop; leaking it rather than freeing memory its task may use";
      (void)vm.release();
      expectCleanPsram = false;
    }
    vm.reset();  // the task's writes stop before the fakes are checked
    store.reset();
    ScreenTest::TearDown();
  }

  // RoundPlayer's side: the game as the fake card holds it, played by the scenario's taps with every local seat not
  // drawn.
  games_check::RoundReport playWithRoundPlayer(const Scenario& scenario) {
    MatchStore refStore;
    EXPECT_TRUE(refStore.allocate(scenario.id.c_str(), 0));
    GameAssets assets;
    EXPECT_EQ(assets.load(scenario.id.c_str(), refStore.saves(), refStore.slot()), GameAssets::LoadResult::Ok);
    // The manifest of a two-seat, pass-only, hidden game, as the check reads it.
    auto manifest = makeUniqueNoThrow<GameCore::Manifest>(match::manifestOf(scenario.id));
    auto settings = makeUniqueNoThrow<GameCore::ManifestSettings>();  // large: on the heap
    EXPECT_TRUE(manifest && settings);
    if (!manifest || !settings) return {};
    manifest->modes = GameCore::Manifest::MODE_PASS;
    manifest->seatsMin = 2;
    manifest->seatsMax = 2;
    manifest->hidden = true;
    games_check::GameFacts facts;
    facts.manifest = manifest.get();
    facts.settings = settings.get();
    facts.hostMaxSeats = 2;
    facts.hostModes = GameCore::Manifest::MODE_PASS;

    std::string error;
    auto script = games_check::OwnedVm::create(assets.sources(), {}, assets.images(), 1, error);
    EXPECT_TRUE(script) << error;
    games_check::Round round;
    EXPECT_TRUE(games_check::loadRound(std::move(script), "pinned", scenario.round, facts, round, error)) << error;
    games_check::GameUnderCheck under;
    under.sources = &assets.sources();
    under.images = &assets.images();
    under.facts = facts;
    games_check::PlayOptions options;
    options.drawEveryLocalSeat = false;
    return games_check::playRound(round, under, options);
  }

  // GameVM's side: the match's own sequence for a hidden round, as GameVmTest's hidden tests drive it.
  void playWithGameVm(const Scenario& scenario) {
    store = makeUniqueNoThrow<MatchStore>();
    ASSERT_NE(store, nullptr);
    ASSERT_TRUE(store->allocate(scenario.id.c_str(), static_cast<uint32_t>(fakertos::S().nowMs.load())));
    GameAssets assets;
    ASSERT_EQ(assets.load(scenario.id.c_str(), store->saves(), store->slot()), GameAssets::LoadResult::Ok);
    vm = GameVM::create(std::move(assets), viewport, replay, scenario.id.c_str(), store->slot(),
                        GameCore::Roster::pass(2), true);
    ASSERT_NE(vm, nullptr);
    ASSERT_TRUE(vm->start());
    ASSERT_TRUE(waitFor([&] { return vm->committed().pending(); }));
    // The hand-off screen is up and nothing is drawn; the first player takes the device.
    uint32_t request = vm->showTurnSeat();
    ASSERT_TRUE(waitFor([&] { return vm->seatShownRequest() == request; }));
    uint32_t passes = 0;
    for (size_t tap = 0; tap < scenario.afters.size(); ++tap) {
      const uint32_t frames = vm->frameGen();
      vm->postInput(tapAt(scenario.taps[tap].first, scenario.taps[tap].second), frames);
      switch (scenario.afters[tap]) {
        case After::Stays:
          ASSERT_TRUE(waitFor([&] { return vm->frameGen() > frames; })) << "tap " << tap + 1;
          break;
        case After::Passes:
          ++passes;
          ASSERT_TRUE(waitFor([&] { return vm->turnsPassed() == passes; })) << "tap " << tap + 1;
          request = vm->showTurnSeat();
          ASSERT_TRUE(waitFor([&] { return vm->seatShownRequest() == request; })) << "seat after tap " << tap + 1;
          break;
        case After::Ends:
          ASSERT_TRUE(waitFor([&] { return vm->roundsEnded() == 1u; })) << "the round's end";
          break;
      }
    }
  }

  // The lines the game itself logged (ch.log), in order: the VM logs each as "INF <game id>: <line>".
  static std::vector<std::string> gameLines(const std::string& id) {
    const std::string prefix = "INF " + id + ": ";
    std::vector<std::string> lines;
    for (const std::string& line : fakelog::snapshot()) {
      if (line.compare(0, prefix.size(), prefix) == 0) lines.push_back(line.substr(prefix.size()));
    }
    return lines;
  }

  FrameReplay replay;
  GameViewport viewport;
  std::unique_ptr<MatchStore> store;  // declared before vm: outlives the task that posts to its slot
  std::unique_ptr<GameVM> vm;
};

// Plays `scenario` through RoundPlayer and then GameVM, and compares what the game logged.
#define EXPECT_THE_SAME_FLOW(scenario, expectedLines)                                  \
  do {                                                                                 \
    const games_check::RoundReport played = playWithRoundPlayer(scenario);             \
    ASSERT_TRUE(played.ok()) << played.failures.front();                               \
    ASSERT_EQ(played.log.size(), (expectedLines)) << "the lines the round should log"; \
    fakelog::clearLines();                                                             \
    playWithGameVm(scenario);                                                          \
    ASSERT_FALSE(HasFatalFailure());                                                   \
    const std::vector<std::string> device = gameLines((scenario).id);                  \
    EXPECT_EQ(played.log, device);                                                     \
    EXPECT_FALSE(vm->failed());                                                        \
    std::vector<std::string> seatsDrawn;                                               \
    for (const games_check::DrawnFrame& frame : played.frames) {                       \
      seatsDrawn.push_back("draw for seat " + std::to_string(frame.seat));             \
    }                                                                                  \
    std::vector<std::string> deviceDraws;                                              \
    for (const std::string& line : device) {                                           \
      if (line.compare(0, 8, "draw for") == 0) deviceDraws.push_back(line);            \
    }                                                                                  \
    EXPECT_EQ(seatsDrawn, deviceDraws);                                                \
  } while (0)

TEST_F(HiddenFlowPinTest, RoundPlayersHiddenFlowDrawsAndDeliversInputInTheOrderGameVMDoes) {
  installFixture("pass-hidden");
  // Four moves, each of which passes the turn but the last, which ends the round.
  const Scenario scenario{"pass-hidden",
                          FOUR_TAPS,
                          {After::Passes, After::Passes, After::Passes, After::Ends},
                          {{100, 200}, {100, 200}, {100, 200}, {100, 200}}};
  EXPECT_THE_SAME_FLOW(scenario, 18u);
}

// A move that keeps the turn and a rejected tap redraw the turn seat alone (stepHandOff's `passed` is false): no
// Result, no hand-off, the same seat's frame again.
TEST_F(HiddenFlowPinTest, ATurnKeptAndARejectedTapAreDrawnAsGameVMDrawsThem) {
  installGame("keeps-turn", games_check::test::HIDDEN_KEEPS_TURN_GAME);
  const Scenario scenario{"keeps-turn",
                          games_check::test::HIDDEN_KEEPS_TURN_ROUND,
                          {After::Stays, After::Stays, After::Passes, After::Stays, After::Ends},
                          {{100, 200}, {10, 200}, {100, 200}, {100, 200}, {100, 200}}};
  EXPECT_THE_SAME_FLOW(scenario, games_check::test::HIDDEN_KEEPS_TURN_LOG.size());
  EXPECT_EQ(gameLines("keeps-turn"), games_check::test::HIDDEN_KEEPS_TURN_LOG);
}

}  // namespace

// RoundPlayer's canvas sizes are doubles of GameViewport::forRenderer on a board: the 480 x 800 portrait screen less
// the board profile's bezel insets (top, right, bottom, left), which the host cannot ask a profile for. The Sticky's
// are the default {9, 3, 3, 3} and the X4 Pro's {9, 7, 3, 7} (freeink-sdk BoardConfig.h, XTEINK_X4_PRO; the simulator's
// shim, .claude/skills/run-crosshatch-player/shim/BoardConfig.h, sets the same). The sizes are more permissive than a
// board in one way: they ignore orientation, and every game plays portrait.
TEST(CanvasSizesTest, AreTheViewportsOfTheTwoBoardsInsets) {
  GfxRenderer sticky(480, 800);
  sticky.setInsets(9, 3, 3, 3);
  const GameViewport stickyView = GameViewport::forRenderer(sticky);
  EXPECT_EQ(stickyView.width(), games_check::CANVAS_474.width);
  EXPECT_EQ(stickyView.height(), games_check::CANVAS_474.height);
  GfxRenderer x4pro(480, 800);
  x4pro.setInsets(9, 7, 3, 7);
  const GameViewport x4proView = GameViewport::forRenderer(x4pro);
  EXPECT_EQ(x4proView.width(), games_check::CANVAS_466.width);
  EXPECT_EQ(x4proView.height(), games_check::CANVAS_466.height);
  // Where the X4 Pro's canvas starts: (7, 9), an even x + y, which the note tiles' dither phase relies on.
  EXPECT_EQ(x4proView.originX(), 7);
  EXPECT_EQ(x4proView.originY(), 9);
}

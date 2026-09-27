#include <gtest/gtest.h>

#include <string>

#include "GameInput.h"
#include "GameTimer.h"
#include "LuaGameFixture.h"
#include "SoloRounds.h"

// The solo round loop as the GameVM task runs it (AD-21): the ended-round count
// the match watches, and Play again on the same Session.

using namespace GameScript;
using GameScriptTestSupport::LuaGameTest;
using GameScriptTestSupport::readFixture;

namespace {

class SoloRoundsTest : public LuaGameTest {
 protected:
  // Loads the game and starts its first round through `rounds`, as GameVM::run does.
  static Outcome begin(SessionGame& game, SoloRounds& rounds) {
    const Outcome loaded = game.game.load();
    if (loaded != Outcome::Ok) return loaded;
    return rounds.start(*game.session);
  }
  static InputEvent tapAt(int16_t y) { return InputEvent{InputKind::Tap, 100, y}; }

  static InputEvent touch(InputKind kind, int16_t x, int16_t y, GameCore::SwipeDir dir = GameCore::SwipeDir::None) {
    InputEvent event{kind, x, y};
    event.dir = dir;
    return event;
  }

  // Moves the clock to the pending timer and steps its event, as GameVM::pollTimer
  // and run do; false when no timer was due.
  bool tick(SoloRounds& rounds, LuaGame& game, uint64_t ms) {
    clock.advance(ms);
    InputEvent event;
    event.kind = InputKind::Timer;
    if (!game.timer().takeDue(clock.nowMs(), event.serial)) return false;
    EXPECT_EQ(rounds.step(event), Outcome::Ok) << game.errorMessage();
    return true;
  }

  Refresh frontRefresh() {
    Refresh hint = Refresh::Fast;
    frames.readFront([&](const DisplayList& list) { hint = list.refresh(); });
    return hint;
  }
};

TEST_F(SoloRoundsTest, InputQueueClearDropsEveryEvent) {
  InputQueue queue;
  for (int i = 0; i < 3; ++i) queue.push(tapAt(static_cast<int16_t>(i)));
  queue.clear();
  InputEvent event;
  EXPECT_FALSE(queue.pop(event));
  queue.push(tapAt(7));
  ASSERT_TRUE(queue.pop(event));
  EXPECT_EQ(event.y, 7);
}

TEST_F(SoloRoundsTest, EachRoundEndIsCountedOnce) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 0u);
  for (int i = 1; i <= 4; ++i) ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 0u);
  ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
  EXPECT_TRUE(contains(frontText().c_str(), "Over events: 1")) << frontText();  // drawn before the count
  // Taps after the end still reach input, but neither move nor count again.
  ASSERT_EQ(rounds.step(tapAt(300)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
  EXPECT_EQ(game.session->ver(), 6u);
}

TEST_F(SoloRoundsTest, PlayAgainDropsQueuedTapsCancelsTheTimerAndKeepsCountingVer) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  for (int i = 1; i <= 5; ++i) ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(rounds.roundsEnded(), 1u);

  // Taps 6 and 7 still queued (a slow input), and a timer armed, when Play again comes.
  queue.push(tapAt(210));
  queue.push(tapAt(220));
  uint32_t firedSerial = 0;
  game.game.timer().arm(clock.nowMs(), TIMER_MIN_MS);
  clock.now += TIMER_MIN_MS;
  ASSERT_TRUE(game.game.timer().takeDue(clock.nowMs(), firedSerial));
  game.game.timer().arm(clock.nowMs(), 5000);

  EXPECT_FALSE(rounds.takePlayAgain());
  rounds.requestPlayAgain();
  InputEvent event;
  EXPECT_FALSE(queue.pop(event)) << "Play again keeps a tap aimed at the last round";
  ASSERT_TRUE(rounds.takePlayAgain());
  EXPECT_FALSE(rounds.takePlayAgain());  // once
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();

  EXPECT_EQ(game.session->ver(), 7u);
  EXPECT_FALSE(game.session->status().over);
  EXPECT_FALSE(game.game.timer().pending());
  EXPECT_TRUE(contains(frontText().c_str(), "Taps: 0 of 5")) << frontText();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
  // A timer event fired in the last round is stale now and never reaches input.
  InputEvent stale;
  stale.kind = InputKind::Timer;
  stale.serial = firedSerial;
  EXPECT_FALSE(game.game.timer().accepts(stale));
  ASSERT_EQ(rounds.step(stale), Outcome::Ok);
  EXPECT_EQ(game.session->ver(), 7u);

  for (int i = 1; i <= 5; ++i) ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 2u);
  EXPECT_EQ(game.session->ver(), 12u);
}

TEST_F(SoloRoundsTest, PlayAgainRunsSetupAgain) {
  useSource("main",
            "local setups = 0\n"
            "return { setup = function() setups = setups + 1 return { n = setups } end,\n"
            "  status = function() return { turn = 1 } end, apply = function(s) return s end,\n"
            "  draw = function(s) ch.gfx.text(0, 0, 'setup ' .. s.n, 'small', 'black') end,\n"
            "  input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "setup 1");
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "setup 2");
  EXPECT_EQ(game.session->ver(), 2u);
}

TEST_F(SoloRoundsTest, ARoundOverAtSetupIsCountedEachTime) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  status = function() return { over = true, winners = {} } end, apply = function(s) return s end,\n"
            "  draw = function() end, input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 2u);
}

TEST_F(SoloRoundsTest, ASetupErrorOnPlayAgainEndsTheSession) {
  useSource("main",
            "local n = 0\n"
            "return { setup = function() n = n + 1 if n > 1 then error('second setup') end return {} end,\n"
            "  status = function() return { over = true, winners = {} } end, apply = function(s) return s end,\n"
            "  draw = function() end, input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  EXPECT_EQ(rounds.restart(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "second setup")) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
}

// The solo fixture, the closing device run's game (Done-when 1), played to game over
// with every input kind, then again after Play again, then reopened.
TEST_F(SoloRoundsTest, TheSoloFixturePlaysToGameOverAndKeepsItsStore) {
  using GameCore::SwipeDir;
  useSource("main", readFixture("solo/main.lua"));
  {
    SessionGame game(*this);
    InputQueue queue;
    SoloRounds rounds(game.game.timer(), queue);
    ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
    EXPECT_TRUE(hasText(frontCommands(), "1 of 8: Tap the circle")) << frontText();
    EXPECT_TRUE(hasText(frontCommands(), "Rounds finished 0")) << frontText();
    EXPECT_EQ(frontRefresh(), Refresh::Full);

    EXPECT_FALSE(tick(rounds, game.game, TIMER_MIN_MS));
    ASSERT_TRUE(tick(rounds, game.game, 4000));
    EXPECT_TRUE(hasText(frontCommands(), "55 s left")) << frontText();
    EXPECT_EQ(frontRefresh(), Refresh::Fast);

    const InputEvent moves[] = {
        touch(InputKind::Tap, 140, 300),
        touch(InputKind::LongPress, 330, 430),
        touch(InputKind::Swipe, 240, 380, SwipeDir::Left),
        touch(InputKind::Tap, 100, 500),  // off the circle: a miss
        touch(InputKind::Tap, 340, 280),
        touch(InputKind::Swipe, 240, 380, SwipeDir::Up),
        touch(InputKind::LongPress, 160, 440),
        touch(InputKind::Swipe, 240, 380, SwipeDir::Right),
    };
    for (const InputEvent& move : moves) ASSERT_EQ(rounds.step(move), Outcome::Ok) << game.errorMessage();
    EXPECT_TRUE(hasText(frontCommands(), "Last swipe: right")) << frontText();
    EXPECT_TRUE(hasText(frontCommands(), "Hits 7  Misses 1")) << frontText();
    EXPECT_TRUE(hasText(frontCommands(), "8 of 8: Swipe down")) << frontText();
    EXPECT_EQ(rounds.roundsEnded(), 0u);

    ASSERT_EQ(rounds.step(touch(InputKind::Swipe, 240, 380, SwipeDir::Down)), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(rounds.roundsEnded(), 1u);
    EXPECT_TRUE(game.session->status().over);
    EXPECT_FALSE(game.game.timer().pending()) << "input cancels the timer on over";
    // 8 hits, 55 s left, 1 miss: 80 + 55 - 5.
    for (const char* line :
         {"Round over", "Score 130, best 130", "Over event received", "Rounds finished 1", "Best 130"}) {
      EXPECT_TRUE(hasText(frontCommands(), line)) << line << "\n" << frontText();
    }
    EXPECT_EQ(frontRefresh(), Refresh::Half);
    EXPECT_TRUE(store.dirty());

    // Play again: a fresh round, the store kept.
    rounds.requestPlayAgain();
    ASSERT_TRUE(rounds.takePlayAgain());
    ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
    for (const char* line : {"Round 2", "1 of 8: Tap the circle", "60 s left", "Rounds finished 1", "Best 130"}) {
      EXPECT_TRUE(hasText(frontCommands(), line)) << line << "\n" << frontText();
    }
    EXPECT_EQ(frontRefresh(), Refresh::Full);

    // This round runs out of time: twelve 5 s ticks.
    for (int i = 1; i <= 12; ++i) ASSERT_TRUE(tick(rounds, game.game, 5000)) << i;
    EXPECT_EQ(rounds.roundsEnded(), 2u);
    EXPECT_FALSE(game.game.timer().pending());
    for (const char* line : {"Score 0, best 130", "Rounds finished 2", "Best 130"}) {
      EXPECT_TRUE(hasText(frontCommands(), line)) << line << "\n" << frontText();
    }
  }

  // Reopening (or a restart, which restores the same bytes) reads the kept store.
  SessionGame reopened(*this);
  ASSERT_EQ(reopened.start(), Outcome::Ok) << reopened.errorMessage();
  for (const char* line : {"Round 3", "Rounds finished 2", "Best 130"}) {
    EXPECT_TRUE(hasText(frontCommands(), line)) << line << "\n" << frontText();
  }
}

}  // namespace

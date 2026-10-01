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
    if (!game.timer().takeDueEvent(clock.nowMs(), event)) return false;
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
  InputEvent fired;
  game.game.timer().arm(clock.nowMs(), TIMER_MIN_MS);
  clock.now += TIMER_MIN_MS;
  ASSERT_TRUE(game.game.timer().takeDueEvent(clock.nowMs(), fired));
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
  EXPECT_FALSE(game.game.timer().accepts(fired));
  ASSERT_EQ(rounds.step(fired), Outcome::Ok);
  EXPECT_EQ(game.session->ver(), 7u);

  for (int i = 1; i <= 5; ++i) ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 2u);
  EXPECT_EQ(game.session->ver(), 12u);
}

// The match asks for no render after Play again until this count moves (the
// retro's R3), so it must move only once the new round's first frame is out.
TEST_F(SoloRoundsTest, ARoundCountsAsStartedOnceItsFirstFrameIsPublished) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  EXPECT_EQ(rounds.roundsStarted(), 0u);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsStarted(), 1u);
  EXPECT_EQ(frames.frameGen(), 1u);
  for (int i = 1; i <= 5; ++i) ASSERT_EQ(rounds.step(tapAt(200)), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(rounds.roundsEnded(), 1u);
  EXPECT_EQ(rounds.roundsStarted(), 1u);  // steps start nothing

  // A step that was already running when Play again came publishes an old-round frame.
  rounds.requestPlayAgain();
  ASSERT_EQ(rounds.step(tapAt(300)), Outcome::Ok) << game.errorMessage();
  const uint32_t oldRoundFrame = frames.frameGen();
  EXPECT_EQ(rounds.roundsStarted(), 1u);
  ASSERT_TRUE(rounds.takePlayAgain());
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsStarted(), 2u);
  EXPECT_GT(frames.frameGen(), oldRoundFrame);
  EXPECT_TRUE(contains(frontText().c_str(), "Taps: 0 of 5")) << frontText();
}

TEST_F(SoloRoundsTest, ARoundWhoseFirstDrawFailsNeverCountsAsStarted) {
  useSource("main",
            "return { setup = function() return {} end, status = function() return { turn = 1 } end,\n"
            "  apply = function(s) return s end, input = function() return nil end,\n"
            "  draw = function() error('no frame', 0) end }");
  SessionGame game(*this);
  InputQueue queue;
  SoloRounds rounds(game.game.timer(), queue);
  EXPECT_EQ(begin(game, rounds), Outcome::ScriptError);
  EXPECT_EQ(rounds.roundsStarted(), 0u);
  EXPECT_EQ(frames.frameGen(), 0u);
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

// ---- the seat-taking steps and the open match (epic-pass-and-play entry 1) ----

// The steps take a seat from the caller and never pick one; the open match's composition (start, restart, step) runs
// them with the seat GameCore::seatShown names. pass-open is the fixture: noughts and crosses whose log names the seat
// of each apply, over, and timer.
class PassRoundsTest : public SoloRoundsTest {
 protected:
  // A tap at the middle of pass-open's cell `cell` (1..9, row by row: 140 px squares from (27, 200)).
  static InputEvent cellTap(const int cell) {
    return InputEvent{InputKind::Tap, static_cast<int16_t>(27 + (cell - 1) % 3 * 140 + 70),
                      static_cast<int16_t>(200 + (cell - 1) / 3 * 140 + 70)};
  }
  size_t logged(const std::string& part) const {
    size_t n = 0;
    for (const std::string& line : log.lines) n += line.find(part) != std::string::npos;
    return n;
  }
  // Where the first log line holding `part` is, or the log's size when none does.
  size_t lineOf(const std::string& part) const {
    for (size_t i = 0; i < log.lines.size(); ++i) {
      if (log.lines[i].find(part) != std::string::npos) return i;
    }
    return log.lines.size();
  }
};

// Every seat has its turn in order; a round ends after three moves with seat 3 the winner. Each seat's ui counts its
// over events, and the frame names the seat it was drawn for.
const char* const THREE_SEATS = R"(
return {
  setup = function(ctx) return { seats = ctx.seats, moves = 0 } end,
  status = function(s)
    if s.moves >= 3 then return { over = true, winners = { 3 } } end
    return { turn = s.moves % s.seats + 1 }
  end,
  apply = function(s, seat) ch.log('apply ' .. seat) s.moves = s.moves + 1 return s end,
  draw = function(s, seat, ui) ch.gfx.text(0, 0, 'seat ' .. seat .. ' overs ' .. (ui.overs or 0), 'small', 'black') end,
  input = function(s, seat, ui, ev)
    if ev.kind == 'over' then ui.overs = (ui.overs or 0) + 1 ch.log('over ' .. seat) end
    return {}
  end }
)";

TEST_F(PassRoundsTest, AMoveFromASeatOffTurnIsDiscarded) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->status().turn, 1);
  const uint32_t frame = frames.frameGen();
  ASSERT_EQ(game.rounds.play(cellTap(1), 2), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->discardedMoves(), 1u);
  EXPECT_FALSE(game.session->pending());
  EXPECT_EQ(game.session->ver(), 1u);
  EXPECT_EQ(logged("apply seat"), 0u);
  EXPECT_EQ(frames.frameGen(), frame) << "play draws nothing";
}

TEST_F(PassRoundsTest, AStepDrawsTheNextTurnSeatAfterAMoveAndItsNextTapIsThatSeats) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Player 1 (X) to move")) << frontText();
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("apply seat 1 cell 1"), 1u);
  EXPECT_TRUE(hasText(frontCommands(), "Player 2 (O) to move")) << frontText();
  ASSERT_EQ(game.step(cellTap(2)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("apply seat 2 cell 2"), 1u);
  EXPECT_TRUE(hasText(frontCommands(), "Player 1 (X) to move")) << frontText();
  EXPECT_EQ(game.session->discardedMoves(), 0u);
}

TEST_F(PassRoundsTest, DrawPublishesTheFrameOfTheSeatItIsGivenWhoeverHasTheTurn) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->status().turn, 2);
  const uint32_t frame = frames.frameGen();
  ASSERT_EQ(game.rounds.draw(1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), frame + 1);
  EXPECT_TRUE(hasText(frontCommands(), "Player 1 (X): Player 2 to move")) << frontText();
  ASSERT_EQ(game.rounds.draw(0), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Everyone: Player 2 to move")) << frontText();
}

TEST_F(PassRoundsTest, ATimerReachesTheTurnSeatAndAStaleOneIsDroppedWithNoDraw) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  // setup's timer fires, then seat 1's tap re-arms it before the event is stepped: the event is stale.
  clock.advance(10000);
  InputEvent fired;
  ASSERT_TRUE(game.game.timer().takeDueEvent(clock.nowMs(), fired));
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  const uint32_t frame = frames.frameGen();
  ASSERT_EQ(game.step(fired), Outcome::Ok);
  EXPECT_EQ(frames.frameGen(), frame) << "a stale timer is dropped with no draw";
  EXPECT_EQ(logged("timer for seat"), 0u);
  // The tap's own timer fires on seat 2's turn and reaches seat 2's input and ui.
  ASSERT_TRUE(tick(game.rounds, game.game, 10000));
  EXPECT_EQ(logged("timer for seat 2"), 1u);
  EXPECT_EQ(logged("timer for seat 1"), 0u);
  EXPECT_TRUE(hasText(frontCommands(), "Nudges 1, overs 0")) << frontText();
  EXPECT_TRUE(hasText(frontCommands(), "Player 2 (O) to move")) << frontText();
}

// Once a pass round is over the composition shows seat 0, so a later tap reaches seat 0's input, and its move is
// discarded: seat 0 is no seat of the roster, and the round is over.
TEST_F(PassRoundsTest, ATapAfterThePassRoundEndsReachesSeatZeroAndItsMoveIsDiscarded) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (const int cell : {1, 2, 4, 3, 7}) ASSERT_EQ(game.step(cellTap(cell)), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(game.session->status().over);
  EXPECT_TRUE(hasText(frontCommands(), "Everyone: Player 1 wins")) << frontText();
  const uint32_t discarded = game.session->discardedMoves();
  const uint32_t ver = game.session->ver();
  ASSERT_EQ(game.step(cellTap(5)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("tap for seat 0"), 1u);
  EXPECT_EQ(logged("apply seat 0"), 0u);
  EXPECT_EQ(logged("cell 5"), 0u);
  EXPECT_EQ(game.session->discardedMoves(), discarded + 1);
  EXPECT_EQ(game.session->ver(), ver);
  EXPECT_TRUE(hasText(frontCommands(), "Everyone: Player 1 wins")) << frontText();
}

TEST_F(PassRoundsTest, ThreeSeatsTakeTurnsAndEachGetsOverOnceBeforeTheFrameForEveryone) {
  useSource("main", THREE_SEATS);
  SessionGame game(*this, GameCore::Roster::pass(3));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "seat 1 overs 0");
  ASSERT_EQ(game.rounds.play(tapAt(10), 3), Outcome::Ok);  // seat 3, off turn
  EXPECT_EQ(logged("apply"), 0u);
  ASSERT_EQ(game.step(tapAt(10)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "seat 2 overs 0");
  ASSERT_EQ(game.step(tapAt(10)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "seat 3 overs 0");
  EXPECT_EQ(game.rounds.roundsEnded(), 0u);
  ASSERT_EQ(game.step(tapAt(10)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("apply 1"), 1u);
  EXPECT_EQ(logged("apply 2"), 1u);
  EXPECT_EQ(logged("apply 3"), 1u);
  for (const char* over : {"over 1", "over 2", "over 3"}) EXPECT_EQ(logged(over), 1u) << over;
  EXPECT_LT(lineOf("over 1"), lineOf("over 2"));
  EXPECT_LT(lineOf("over 2"), lineOf("over 3"));
  EXPECT_EQ(frontText(), "seat 0 overs 0") << "seat 0's ui is its own; no seat's over reached it";
  EXPECT_EQ(game.rounds.roundsEnded(), 1u);
  EXPECT_EQ(game.session->discardedMoves(), 4u);  // seat 3's off-turn move and the three answers to over

  // Play again: seat 1 first, and over comes once more to each seat in the next round.
  ASSERT_EQ(game.playAgain(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "seat 1 overs 1");
  for (int i = 0; i < 3; ++i) ASSERT_EQ(game.step(tapAt(10)), Outcome::Ok) << game.errorMessage();
  for (const char* over : {"over 1", "over 2", "over 3"}) EXPECT_EQ(logged(over), 2u) << over;
  EXPECT_EQ(game.rounds.roundsEnded(), 2u);
}

TEST_F(PassRoundsTest, ARosterWithOneLocalSeatDrawsThatSeatWhilePlayingAndOnceOver) {
  useSource(
      "main",
      "return { setup = function() return { moves = 0 } end,\n"
      "  status = function(s) if s.moves >= 1 then return { over = true, winners = { 2 } } end\n"
      "    return { turn = 2 } end,\n"
      "  apply = function(s, seat) ch.log('apply ' .. seat) s.moves = s.moves + 1 return s end,\n"
      "  draw = function(s, seat) ch.gfx.text(0, 0, 'seat ' .. seat .. ' moves ' .. s.moves, 'small', 'black') end,\n"
      "  input = function(s, seat, ui, ev) if ev.kind == 'over' then ch.log('over ' .. seat) end return {} end }");
  GameCore::Roster roster;  // nearby's shape: two seats, this device plays seat 2
  roster.mode = GameCore::Mode::Nearby;
  roster.seats = 2;
  roster.localSeats = 0b10;
  SessionGame game(*this, roster);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "seat 2 moves 0");
  ASSERT_EQ(game.step(tapAt(10)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("apply 2"), 1u);
  EXPECT_TRUE(game.session->status().over);
  EXPECT_EQ(frontText(), "seat 2 moves 1") << "one local seat: its own frame once over, not seat 0's";
  EXPECT_EQ(logged("over 2"), 1u);
  EXPECT_EQ(logged("over 1"), 0u);
  EXPECT_EQ(game.rounds.roundsEnded(), 1u);
}

TEST_F(PassRoundsTest, BeginPublishesNoFrameAndARoundStartsAtItsFirstDraw) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.game.load(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.rounds.begin(*game.session), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 0u);
  EXPECT_EQ(game.rounds.roundsStarted(), 0u);
  ASSERT_EQ(game.rounds.draw(1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 1u);
  EXPECT_EQ(game.rounds.roundsStarted(), 1u);
  ASSERT_EQ(game.rounds.draw(2), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.rounds.roundsStarted(), 1u) << "once a round";

  game.rounds.requestPlayAgain();
  ASSERT_TRUE(game.rounds.takePlayAgain());
  ASSERT_EQ(game.rounds.beginAgain(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 2u);
  EXPECT_EQ(game.rounds.roundsStarted(), 1u);
  ASSERT_EQ(game.rounds.draw(1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.rounds.roundsStarted(), 2u);
}

}  // namespace

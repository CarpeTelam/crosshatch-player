#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "GameInput.h"
#include "GameTimer.h"
#include "IGameRules.h"
#include "LuaGameFixture.h"
#include "MatchRounds.h"
#include "SeatShown.h"
#include "Session.h"

// The round loop as the GameVM task runs it (AD-21): the ended-round count
// the match watches, and Play again on the same Session.

using namespace GameScript;
using GameScriptTestSupport::LuaGameTest;
using GameScriptTestSupport::readFixture;

namespace {

class MatchRoundsTest : public LuaGameTest {
 protected:
  // Loads the game and starts its first round through `rounds`, as GameVM::run does.
  static Outcome begin(SessionGame& game, MatchRounds& rounds) {
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
  bool tick(MatchRounds& rounds, LuaGame& game, uint64_t ms) {
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

TEST_F(MatchRoundsTest, InputQueueClearDropsEveryEvent) {
  InputQueue queue;
  for (int i = 0; i < 3; ++i) queue.push(tapAt(static_cast<int16_t>(i)));
  queue.clear();
  InputEvent event;
  EXPECT_FALSE(queue.pop(event));
  queue.push(tapAt(7));
  ASSERT_TRUE(queue.pop(event));
  EXPECT_EQ(event.y, 7);
}

TEST_F(MatchRoundsTest, EachRoundEndIsCountedOnce) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
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

TEST_F(MatchRoundsTest, PlayAgainDropsQueuedTapsCancelsTheTimerAndKeepsCountingVer) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
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
TEST_F(MatchRoundsTest, ARoundCountsAsStartedOnceItsFirstFrameIsPublished) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
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

TEST_F(MatchRoundsTest, ARoundWhoseFirstDrawFailsNeverCountsAsStarted) {
  useSource("main",
            "return { setup = function() return {} end, status = function() return { turn = 1 } end,\n"
            "  apply = function(s) return s end, input = function() return nil end,\n"
            "  draw = function() error('no frame', 0) end }");
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  EXPECT_EQ(begin(game, rounds), Outcome::ScriptError);
  EXPECT_EQ(rounds.roundsStarted(), 0u);
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(MatchRoundsTest, PlayAgainRunsSetupAgain) {
  useSource("main",
            "local setups = 0\n"
            "return { setup = function() setups = setups + 1 return { n = setups } end,\n"
            "  status = function() return { turn = 1 } end, apply = function(s) return s end,\n"
            "  draw = function(s) ch.gfx.text(0, 0, 'setup ' .. s.n, 'small', 'black') end,\n"
            "  input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "setup 1");
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "setup 2");
  EXPECT_EQ(game.session->ver(), 2u);
}

TEST_F(MatchRoundsTest, ARoundOverAtSetupIsCountedEachTime) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  status = function() return { over = true, winners = {} } end, apply = function(s) return s end,\n"
            "  draw = function() end, input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  ASSERT_EQ(rounds.restart(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 2u);
}

TEST_F(MatchRoundsTest, ASetupErrorOnPlayAgainEndsTheSession) {
  useSource("main",
            "local n = 0\n"
            "return { setup = function() n = n + 1 if n > 1 then error('second setup') end return {} end,\n"
            "  status = function() return { over = true, winners = {} } end, apply = function(s) return s end,\n"
            "  draw = function() end, input = function() return nil end }");
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  rounds.requestPlayAgain();
  ASSERT_TRUE(rounds.takePlayAgain());
  EXPECT_EQ(rounds.restart(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "second setup")) << game.errorMessage();
  EXPECT_EQ(rounds.roundsEnded(), 1u);
}

// A timer due after the round is over is never delivered, in any mode (e6pre-2, the owner's option (a) on the e5-r6
// entries): a solo round that is over still shows its one local seat, and its timer is dropped all the same. The game
// here never cancels its timer, so only the runtime's drop keeps it out of `input`.
const char* const SOLO_TIMER_GAME = R"(
return {
  setup = function() ch.timer.after(5000) return { taps = 0 } end,
  status = function(s) if s.taps >= 1 then return { over = true, winners = { 1 } } end return { turn = 1 } end,
  apply = function(s) s.taps = s.taps + 1 return s end,
  draw = function() ch.gfx.clear('white') end,
  input = function(s, seat, ui, ev)
    if ev.kind == 'tap' then return {} end
    if ev.kind == 'timer' then ch.log('timer for seat ' .. seat) end
    return nil
  end }
)";

TEST_F(MatchRoundsTest, ASoloTimerDueAfterOverIsDroppedNotDelivered) {
  useSource("main", SOLO_TIMER_GAME);
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(rounds.step(tapAt(300)), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(game.session->status().over);
  EXPECT_EQ(rounds.shownSeat(), 1) << "over, but the one local seat is still the seat shown";
  ASSERT_TRUE(tick(rounds, game.game, 5000));
  size_t seen = 0;
  for (const std::string& line : log.lines) seen += line.find("timer for seat") != std::string::npos;
  EXPECT_EQ(seen, 0u);
  // The predicate itself (play and step both ask it; only a timer after over is late).
  InputEvent late;
  late.kind = InputKind::Timer;
  EXPECT_TRUE(MatchRounds::lateTimer(late, true));
  EXPECT_FALSE(MatchRounds::lateTimer(late, false)) << "a timer before the round's end is never late";
  EXPECT_FALSE(MatchRounds::lateTimer(tapAt(10), true)) << "only a timer is late";
}

TEST_F(MatchRoundsTest, ASoloTimerDueBeforeOverStillReachesTheLocalSeat) {
  useSource("main", SOLO_TIMER_GAME);
  SessionGame game(*this);
  InputQueue queue;
  MatchRounds rounds(game.game.timer(), queue);
  ASSERT_EQ(begin(game, rounds), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(tick(rounds, game.game, 5000));
  ASSERT_FALSE(game.session->status().over);
  size_t seen = 0;
  for (const std::string& line : log.lines) seen += line.find("timer for seat 1") != std::string::npos;
  EXPECT_EQ(seen, 1u);
}

// The solo fixture, the closing device run's game (Done-when 1), played to game over
// with every input kind, then again after Play again, then reopened.
TEST_F(MatchRoundsTest, TheSoloFixturePlaysToGameOverAndKeepsItsStore) {
  using GameCore::SwipeDir;
  useSource("main", readFixture("solo/main.lua"));
  {
    SessionGame game(*this);
    InputQueue queue;
    MatchRounds rounds(game.game.timer(), queue);
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
class PassRoundsTest : public MatchRoundsTest {
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

// A timer that falls due after a pass round is over names seat 0, the frame for everyone, which is never an input seat:
// step and play drop it with no draw, so a game's input() never sees "timer for seat 0" (e5-r6, retrospective F9).
// pass-open's tap for seat 0 re-arms its timer, which is how a timer comes due after the round.
TEST_F(PassRoundsTest, ATimerDueAfterThePassRoundEndsIsDroppedNotDeliveredToSeatZero) {
  useSource("main", readFixture("pass-open/main.lua"));
  SessionGame game(*this, GameCore::Roster::pass(2));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (const int cell : {1, 2, 4, 3, 7}) ASSERT_EQ(game.step(cellTap(cell)), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(game.session->status().over);
  ASSERT_EQ(game.step(cellTap(5)), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(logged("tap for seat 0"), 1u);
  const uint32_t frame = frames.frameGen();
  ASSERT_TRUE(tick(game.rounds, game.game, 10000));
  EXPECT_EQ(logged("timer for seat"), 0u) << "seat 0 is a frame, never an input seat";
  EXPECT_EQ(frames.frameGen(), frame) << "a dropped timer draws nothing";
  // play drops it as well, for a caller that already named the seat.
  ASSERT_EQ(game.step(cellTap(5)), Outcome::Ok) << game.errorMessage();
  clock.advance(10000);
  InputEvent due;
  ASSERT_TRUE(game.game.timer().takeDueEvent(clock.nowMs(), due));
  ASSERT_EQ(game.rounds.play(due, 0), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("timer for seat"), 0u);
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

// A roster with no local seat (firstLocalSeat() is 0 by default, so shownSeat() reads 0 whether or not the round is
// over): a timer due after the round ended is dropped. This passes on the seat-0 test too (a regression guard); the
// predicate check below pins that seat 0 alone no longer means over.
TEST_F(PassRoundsTest, ARosterWithNoLocalSeatDropsATimerDueAfterOverAndNotBefore) {
  useSource("main", R"(
return {
  setup = function() ch.timer.after(5000) return {} end,
  status = function() return { over = true, winners = {} } end,
  apply = function(s) return s end,
  draw = function() ch.gfx.clear('white') end,
  input = function(s, seat, ui, ev) ch.log('input ' .. ev.kind .. ' ' .. seat) return nil end }
)");
  GameCore::Roster roster = GameCore::Roster::pass(2);
  roster.localSeats = 0;
  SessionGame game(*this, roster);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.rounds.shownSeat(), 0) << "no local seat: seat 0, over or not";
  ASSERT_TRUE(game.session->status().over);
  ASSERT_TRUE(tick(game.rounds, game.game, 5000)) << "the timer was armed and fell due";
  EXPECT_EQ(logged("input timer"), 0u);
  EXPECT_FALSE(MatchRounds::lateTimer(InputEvent{InputKind::Timer, 0, 0}, false))
      << "seat 0 alone is not 'over': a round still playing is asked with status.over";
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

// A roster with two of three seats local (epic-play-nearby R10, partly-remote rosters): seats 1 and 2 are this
// device's, seat 3 another's. The game keeps the turn in its state: the first round starts on seat `first`, every later
// round on seat `later` (the module's setup count survives Play again), and a move by seat 1 or 2 passes the turn to
// seat 3, one by seat 3 to seat 1. Every call is logged with its seat; a timer is armed by setup and by each tap, so
// one is pending whoever has the turn.
std::string partlyRemote(const int first, const int later = 3) {
  return "local NEXT = { 3, 3, 1 }\n"
         "local setups = 0\n"
         "return {\n"
         "  setup = function(ctx)\n"
         "    setups = setups + 1\n"
         "    ch.timer.after(5000)\n"
         "    return { turn = setups == 1 and " +
         std::to_string(first) + " or " + std::to_string(later) +
         ", moves = 0 }\n"
         "  end,\n"
         "  status = function(s) return { turn = s.turn } end,\n"
         "  apply = function(s, seat) ch.log('apply ' .. seat) s.turn = NEXT[seat] s.moves = s.moves + 1 return s "
         "end,\n"
         "  draw = function(s, seat, ui) ch.log('draw ' .. seat)\n"
         "    ch.gfx.text(0, 0, 'seat ' .. seat .. ' turn ' .. s.turn .. ' moves ' .. s.moves, 'small', 'black') end,\n"
         "  input = function(s, seat, ui, ev)\n"
         "    ch.log(ev.kind .. ' ' .. seat)\n"
         "    if ev.kind == 'tap' then ch.timer.after(5000) return { go = true } end\n"
         "    if ev.kind == 'timer' then ch.timer.after(5000) end\n"
         "    return nil\n"
         "  end }\n";
}

GameCore::Roster firstTwoOfThreeLocal() {
  GameCore::Roster roster = GameCore::Roster::pass(3);
  roster.mode = GameCore::Mode::Nearby;
  roster.localSeats = 0b011;
  return roster;
}

// Path 1: a local move that passes the turn to a remote seat is drawn, as the seat that last showed (the mover), so the
// screen shows the result of the move instead of the frame from before it.
TEST_F(PassRoundsTest, ALocalMoveThatPassesTheTurnToARemoteSeatIsDrawnAsTheMover) {
  useSource("main", partlyRemote(1));
  SessionGame game(*this, firstTwoOfThreeLocal());
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->status().turn, 1);
  ASSERT_EQ(frames.frameGen(), 1u);
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("apply 1"), 1u);
  ASSERT_EQ(game.session->status().turn, 3);
  EXPECT_EQ(frames.frameGen(), 2u) << "the move is drawn although the turn is another device's";
  EXPECT_EQ(frontText(), "seat 1 turn 3 moves 1");
  EXPECT_EQ(game.rounds.shownSeat(), 1);
  EXPECT_EQ(logged("draw 3"), 0u) << "never another device's seat";
}

// Path 3: a timer due while the turn is remote reaches the local seat that last showed (seat 2 here), once, with one
// draw; so does a tap, whose move is discarded off turn.
TEST_F(PassRoundsTest, ATimerDueWhileTheTurnIsRemoteReachesTheSeatThatLastShowed) {
  useSource("main", partlyRemote(2));
  SessionGame game(*this, firstTwoOfThreeLocal());
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();  // seat 2 moves; the turn is seat 3's
  ASSERT_EQ(game.session->status().turn, 3);
  ASSERT_EQ(game.rounds.shownSeat(), 2);
  const uint32_t frame = frames.frameGen();
  ASSERT_TRUE(tick(game.rounds, game.game, 5000));
  EXPECT_EQ(logged("timer 2"), 1u);
  EXPECT_EQ(logged("timer 1"), 0u);
  EXPECT_EQ(frames.frameGen(), frame + 1) << "one draw";
  EXPECT_EQ(frontText(), "seat 2 turn 3 moves 1");
  // A tap is read by the shown seat, its move discarded off turn.
  const uint32_t discarded = game.session->discardedMoves();
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("tap 2"), 2u);
  EXPECT_EQ(logged("apply"), 1u) << "only the first move was applied";
  EXPECT_EQ(game.session->discardedMoves(), discarded + 1);
}

// Path 4: a round whose first turn is remote publishes the first local seat's frame at start and counts as started,
// and its taps are that seat's, discarded off turn.
TEST_F(PassRoundsTest, ARoundWhoseFirstTurnIsRemoteDrawsTheFirstLocalSeatAndCountsAsStarted) {
  useSource("main", partlyRemote(3));
  SessionGame game(*this, firstTwoOfThreeLocal());
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->status().turn, 3);
  EXPECT_EQ(game.rounds.shownSeat(), 1) << "no seat has shown: the first local seat";
  EXPECT_EQ(frames.frameGen(), 1u);
  EXPECT_EQ(game.rounds.roundsStarted(), 1u);
  EXPECT_EQ(frontText(), "seat 1 turn 3 moves 0");
  EXPECT_EQ(logged("draw 3"), 0u);
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("tap 1"), 1u);
  EXPECT_EQ(logged("apply"), 0u);
  EXPECT_EQ(game.session->discardedMoves(), 1u);
}

// Path 4, Play again: the next round's first turn is seat 3's, and seat 2, which showed last, is drawn: not the first
// local seat, and not nothing.
TEST_F(PassRoundsTest, APlayAgainWhoseFirstTurnIsRemoteDrawsTheSeatThatLastShowed) {
  useSource("main", partlyRemote(2, 3));
  SessionGame game(*this, firstTwoOfThreeLocal());
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->status().turn, 2);
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();  // seat 2 moves
  ASSERT_EQ(game.rounds.roundsStarted(), 1u);
  const uint32_t frame = frames.frameGen();
  ASSERT_EQ(game.playAgain(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->status().turn, 3);
  EXPECT_EQ(game.rounds.roundsStarted(), 2u) << "the new round's first frame was published";
  EXPECT_EQ(frames.frameGen(), frame + 1);
  EXPECT_EQ(frontText(), "seat 2 turn 3 moves 0");
}

// A roster with no local seat shows seat 0 (as before); a timer of a round still playing is dropped, never delivered as
// seat 0, in step and in play, and a tap is read as before.
TEST_F(PassRoundsTest, ARosterWithNoLocalSeatDropsATimerOfARoundStillPlaying) {
  useSource("main", partlyRemote(1));
  GameCore::Roster roster = firstTwoOfThreeLocal();
  roster.localSeats = 0;
  SessionGame game(*this, roster);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_FALSE(game.session->status().over);
  ASSERT_EQ(game.rounds.shownSeat(), 0) << "no local seat: seat 0, as before";
  const uint32_t frame = frames.frameGen();
  ASSERT_TRUE(tick(game.rounds, game.game, 5000));
  EXPECT_EQ(logged("timer"), 0u) << "not delivered, as seat 0 or any seat";
  EXPECT_EQ(frames.frameGen(), frame) << "a dropped timer draws nothing";
  // A tap is not a timer: seat 0 reads it, and it re-arms the timer.
  ASSERT_EQ(game.step(cellTap(1)), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("tap 0"), 1u);
  // play drops the timer as well, for a caller that already named the seat.
  clock.advance(5000);
  InputEvent due;
  ASSERT_TRUE(game.game.timer().takeDueEvent(clock.nowMs(), due));
  ASSERT_EQ(game.rounds.play(due, 0), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(logged("timer"), 0u);
}

// One local seat and an all-local pass roster still deliver a timer of a round still playing (the drop is for a roster
// with no local seat alone).
TEST_F(PassRoundsTest, ARosterWithALocalSeatStillDeliversATimerOfARoundStillPlaying) {
  useSource("main", partlyRemote(1));
  GameCore::Roster oneLocal = firstTwoOfThreeLocal();
  oneLocal.localSeats = 0b010;
  SessionGame game(*this, oneLocal);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(tick(game.rounds, game.game, 5000));
  EXPECT_EQ(logged("timer 2"), 1u) << "the one local seat";
}

// ---- a nearby round: a device with no state yet, and the STATE it adopts (epic-play-nearby entry 9) ----

// Two devices over one nearby game. One LuaGame and arena serve both (the real devices each have their own; the game
// keeps no state between calls but each seat's ui, and the two devices play different seats), and the host's
// SessionGame owns the host's loop; the follower is a Session and a MatchRounds of its own over the same game. A
// shipment is handed across by hand, as NearbySession does between the Session's pendingShipment and adopt.
class NearbyRoundsTest : public MatchRoundsTest {
 protected:
  static InputEvent cellTap(const int cell) {
    return InputEvent{InputKind::Tap, static_cast<int16_t>(27 + (cell - 1) % 3 * 140 + 70),
                      static_cast<int16_t>(200 + (cell - 1) / 3 * 140 + 70)};
  }
  // The nearby roster of two seats, the one local seat `seat`.
  static GameCore::Roster roster(const uint8_t seat) { return GameCore::Roster::nearby(2, seat, API_LEVEL); }
  size_t logged(const std::string& part) const {
    size_t n = 0;
    for (const std::string& line : log.lines) n += line.find(part) != std::string::npos;
    return n;
  }

  // The follower's side: seat 2's Session and loop over the host's LuaGame, destroyed before it.
  struct Follower {
    // The Session is on the heap here, not in the arena: the arena's reserve holds one (GameVM's), and this test's
    // host has taken it.
    explicit Follower(SessionGame& host)
        : owned(std::make_unique<GameCore::Session>(roster(2), host.game)),
          session(owned.get()),
          rounds(host.game.timer(), queue) {}
    std::unique_ptr<GameCore::Session> owned;
    GameCore::Session* session;
    GameScript::InputQueue queue;
    MatchRounds rounds;
  };

  // Seat 1 (the host) plays 1, 2, 3 and seat 2 plays 4, 5, each move applied on its mover's device and handed to the
  // other; seat 1's third move ends the round.
  static void playToOver(SessionGame& host, Follower& follower) {
    const int cells[] = {1, 4, 2, 5, 3};
    for (int move = 0; move < 5; ++move) {
      if (move % 2 == 0) {
        ASSERT_EQ(host.step(cellTap(cells[move])), Outcome::Ok) << host.errorMessage();
        ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::Adopted) << move;
      } else {
        ASSERT_EQ(follower.rounds.step(cellTap(cells[move])), Outcome::Ok) << host.errorMessage();
        ASSERT_TRUE(follower.session->shipmentPending()) << move;
        ASSERT_EQ(hand(*follower.session, 2, *host.session, host.rounds), GameCore::Adoption::Adopted) << move;
      }
    }
  }

  // The shipment `from` has pending, handed to `to` as a STATE from seat `fromSeat`, and what adopt said. The shipment
  // is shipped (the link took it) whether or not `to` adopts it.
  static GameCore::Adoption hand(GameCore::Session& from, const uint8_t fromSeat, GameCore::Session& to,
                                 MatchRounds& toRounds, Outcome* outcome = nullptr) {
    GameCore::Shipment shipment;
    EXPECT_TRUE(from.pendingShipment(shipment));
    return handShipment(shipment, fromSeat, to, toRounds, outcome, &from);
  }
  static GameCore::Adoption handShipment(const GameCore::Shipment& shipment, const uint8_t fromSeat,
                                         GameCore::Session& to, MatchRounds& toRounds, Outcome* outcome = nullptr,
                                         GameCore::Session* from = nullptr) {
    GameCore::PeerState peer;
    peer.fromSeat = fromSeat;
    peer.ver = shipment.ver;
    peer.status = shipment.status;
    peer.snapshot = shipment.snapshot;
    GameCore::Adoption adoption = GameCore::Adoption::NotPeer;
    const Outcome adopted = to.adopt(peer, adoption);
    EXPECT_EQ(adopted, Outcome::Ok);
    const Outcome drawn = adopted == Outcome::Ok ? toRounds.adopted(adoption) : adopted;
    EXPECT_EQ(drawn, Outcome::Ok);
    if (outcome) *outcome = drawn;
    if (from) from->shipped();
    return adoption;
  }
};

TEST_F(NearbyRoundsTest, AFollowerBeforeTheFirstStateReadsNothingAndCountsNoRound) {
  useSource("main", readFixture("nearby/main.lua"));
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.game.load(), Outcome::Ok) << host.errorMessage();
  follower.rounds.attach(*follower.session);
  EXPECT_FALSE(follower.session->hasState());
  EXPECT_EQ(follower.rounds.roundsStarted(), 0u);
  EXPECT_EQ(frames.frameGen(), 0u);
  ASSERT_EQ(follower.rounds.step(cellTap(1)), Outcome::Ok) << host.errorMessage();
  ASSERT_EQ(follower.rounds.step(InputEvent{InputKind::Timer, 0, 0}), Outcome::Ok);
  EXPECT_EQ(frames.frameGen(), 0u) << "a draw would publish an empty first frame";
  EXPECT_EQ(follower.rounds.roundsStarted(), 0u);
  EXPECT_EQ(follower.rounds.roundsEnded(), 0u);
  EXPECT_EQ(logged("tap for seat"), 0u) << "no input reached the game";
}

TEST_F(NearbyRoundsTest, AttachThenTheHostsFirstStateDrawsTheOwnSeatAndCountsTheRoundStarted) {
  useSource("main", readFixture("nearby/main.lua"));
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.start(), Outcome::Ok) << host.errorMessage();
  EXPECT_TRUE(contains(frontText().c_str(), "Player 1 (X) to move")) << frontText();
  ASSERT_TRUE(host.session->shipmentPending());
  follower.rounds.attach(*follower.session);
  const uint32_t before = frames.frameGen();
  EXPECT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  EXPECT_EQ(frames.frameGen(), before + 1);
  EXPECT_TRUE(contains(frontText().c_str(), "Player 2 (O): Player 1 to move")) << frontText();
  EXPECT_EQ(follower.rounds.shownSeat(), 2);
  EXPECT_EQ(follower.rounds.roundsStarted(), 1u) << "counted once the first frame is published";
  EXPECT_EQ(follower.rounds.lastLocalSeat(), 2);
  EXPECT_FALSE(host.session->shipmentPending());
  // It reads input from now on.
  ASSERT_EQ(follower.rounds.step(cellTap(1)), Outcome::Ok) << host.errorMessage();
  EXPECT_EQ(logged("tap for seat 2"), 1u);
  EXPECT_EQ(follower.session->discardedMoves(), 1u) << "off turn: the move is discarded, nothing to ship";
  EXPECT_FALSE(follower.session->shipmentPending());
}

TEST_F(NearbyRoundsTest, AMoveTheHostMadeIsAdoptedAndDrawnAndARefusedStateDrawsNothing) {
  useSource("main", readFixture("nearby/main.lua"));
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.start(), Outcome::Ok) << host.errorMessage();
  follower.rounds.attach(*follower.session);
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  ASSERT_EQ(host.step(cellTap(5)), Outcome::Ok) << host.errorMessage();
  ASSERT_TRUE(host.session->shipmentPending());
  GameCore::Shipment move;
  ASSERT_TRUE(host.session->pendingShipment(move));
  const uint32_t before = frames.frameGen();
  EXPECT_EQ(handShipment(move, 1, *follower.session, follower.rounds), GameCore::Adoption::Adopted);
  EXPECT_EQ(frames.frameGen(), before + 1);
  EXPECT_TRUE(contains(frontText().c_str(), "Player 2 (O) to move")) << frontText();
  EXPECT_EQ(follower.rounds.roundsStarted(), 1u) << "a move starts no round";
  // The same STATE again is stale: the Session changes nothing and the loop draws nothing.
  EXPECT_EQ(handShipment(move, 1, *follower.session, follower.rounds), GameCore::Adoption::Stale);
  EXPECT_EQ(frames.frameGen(), before + 1);
  // A STATE from the seat this device plays is no peer's.
  EXPECT_EQ(handShipment(move, 2, *follower.session, follower.rounds), GameCore::Adoption::NotPeer);
  EXPECT_EQ(frames.frameGen(), before + 1);
}

// X plays 1, 2, 3 and O 4, 5: the round's fifth move, seat 1's third, wins.
TEST_F(NearbyRoundsTest, AnAdoptedOverCountsTheRoundsEndOnceAndTheNextRoundStartsAfterIt) {
  useSource("main", readFixture("nearby/main.lua"));
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.start(), Outcome::Ok) << host.errorMessage();
  follower.rounds.attach(*follower.session);
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  ASSERT_NO_FATAL_FAILURE(playToOver(host, follower));
  EXPECT_TRUE(host.session->status().over);
  EXPECT_TRUE(follower.session->status().over);
  EXPECT_EQ(follower.session->ver(), host.session->ver());
  EXPECT_EQ(follower.rounds.roundsEnded(), 1u);
  EXPECT_EQ(host.rounds.roundsEnded(), 1u) << "the mover counted the end at its own step";
  EXPECT_EQ(logged("over for seat 1 board 111220000"), 1u);
  EXPECT_EQ(logged("over for seat 2 board 111220000"), 1u) << "one over event on each device";
  EXPECT_FALSE(follower.session->shipmentPending());

  // The host's Play again: the next round's STATE counts the follower's start after the end it already counted.
  host.rounds.requestPlayAgain();
  ASSERT_TRUE(host.rounds.takePlayAgain());
  ASSERT_EQ(host.rounds.restart(), Outcome::Ok) << host.errorMessage();
  ASSERT_TRUE(host.session->shipmentPending());
  EXPECT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  EXPECT_EQ(follower.rounds.roundsEnded(), 1u);
  EXPECT_EQ(follower.rounds.roundsStarted(), 2u);
  EXPECT_FALSE(follower.session->status().over);
  EXPECT_TRUE(contains(frontText().c_str(), "Player 2 (O): Player 1 to move")) << frontText();
  // The new round ends and is counted too: the adopted start cleared the last round's count.
  ASSERT_NO_FATAL_FAILURE(playToOver(host, follower));
  EXPECT_EQ(follower.rounds.roundsEnded(), 2u);
  EXPECT_EQ(host.rounds.roundsEnded(), 2u);
}

// The VM leaves Play again until the over shipment has gone (entry 7, C6); the request waits in MatchRounds, and the
// restart that follows still counts the new round.
TEST_F(NearbyRoundsTest, APlayAgainRequestKeptUntilTheShipmentWentStillStartsAndCountsTheNextRound) {
  useSource("main", readFixture("nearby/main.lua"));
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.start(), Outcome::Ok) << host.errorMessage();
  follower.rounds.attach(*follower.session);
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  const int cells[] = {1, 4, 2, 5, 3};
  for (int move = 0; move < 4; ++move) {
    if (move % 2 == 0) {
      ASSERT_EQ(host.step(cellTap(cells[move])), Outcome::Ok) << host.errorMessage();
      ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::Adopted);
    } else {
      ASSERT_EQ(follower.rounds.step(cellTap(cells[move])), Outcome::Ok) << host.errorMessage();
      ASSERT_EQ(hand(*follower.session, 2, *host.session, host.rounds), GameCore::Adoption::Adopted);
    }
  }
  ASSERT_EQ(host.step(cellTap(3)), Outcome::Ok) << host.errorMessage();  // the winning move; its shipment is pending
  ASSERT_TRUE(host.session->shipmentPending());
  ASSERT_EQ(host.rounds.roundsEnded(), 1u);
  host.rounds.requestPlayAgain();  // the owner leaves it: the over shipment has not gone
  EXPECT_TRUE(host.session->shipmentPending());
  EXPECT_EQ(host.rounds.roundsStarted(), 1u);
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::Adopted);  // it went
  ASSERT_FALSE(host.session->shipmentPending());
  ASSERT_TRUE(host.rounds.takePlayAgain()) << "the request was kept";
  ASSERT_EQ(host.rounds.restart(), Outcome::Ok) << host.errorMessage();
  EXPECT_EQ(host.rounds.roundsStarted(), 2u);
  EXPECT_EQ(host.rounds.roundsEnded(), 1u);
}

// The follower's timer is the old round's when the host starts the next: adopting RoundStarted cancels it, as
// beginAgain does for the host's own restart, so it neither stays pending nor falls due into the new round. The shared
// LuaGame (see NearbyRoundsTest) has one timer, so the host restarts first, and the follower arms its own afterwards.
TEST_F(NearbyRoundsTest, AdoptingTheHostsNextRoundCancelsTheTimerTheFollowerArmedInTheLastOne) {
  std::string source = readFixture("nearby/main.lua");
  const std::string tap = "    ui.message = nil\n";
  ASSERT_NE(source.find(tap), std::string::npos);
  source.replace(source.find(tap), tap.size(), tap + "    ch.timer.after(5000)\n");
  const std::string rejected = "  elseif ev.kind == \"rejected\" then\n";
  ASSERT_NE(source.find(rejected), std::string::npos);
  source.replace(source.find(rejected), rejected.size(),
                 "  elseif ev.kind == \"timer\" then\n    ch.log(\"timer for seat \" .. seat)\n" + rejected);
  useSource("main", source);
  SessionGame host(*this, roster(1));
  Follower follower(host);
  ASSERT_EQ(host.start(), Outcome::Ok) << host.errorMessage();
  follower.rounds.attach(*follower.session);
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  ASSERT_NO_FATAL_FAILURE(playToOver(host, follower));
  host.rounds.requestPlayAgain();
  ASSERT_TRUE(host.rounds.takePlayAgain());
  ASSERT_EQ(host.rounds.restart(), Outcome::Ok) << host.errorMessage();
  // Still in the finished round, the follower taps: input arms a timer for the round that is over.
  ASSERT_EQ(follower.rounds.step(cellTap(7)), Outcome::Ok) << host.errorMessage();
  ASSERT_TRUE(host.game.timer().pending());
  ASSERT_EQ(hand(*host.session, 1, *follower.session, follower.rounds), GameCore::Adoption::RoundStarted);
  EXPECT_FALSE(host.game.timer().pending());
  clock.advance(5000);
  InputEvent due;
  EXPECT_FALSE(host.game.timer().takeDueEvent(clock.nowMs(), due)) << "nothing falls due into the new round";
  EXPECT_EQ(logged("timer for seat"), 0u);
}

// Stands in for a game whose status names a turn that is no seat of the roster. LuaGame refuses such a status
// (popSeat takes only 1..n), so only rules that are not Lua reach MatchRounds' fail-closed branches. One-byte state;
// every draw and input call is counted.
class NoSeatTurnRules : public GameCore::IGameRules {
 public:
  Outcome setup(const GameCore::GameContext&, std::span<const uint8_t>& state) override {
    state = {&byte, 1};
    return Outcome::Ok;
  }
  Outcome status(std::span<const uint8_t>, const GameCore::Roster&, GameCore::Status& out) override {
    out = GameCore::Status{};
    out.turn = 3;
    return Outcome::Ok;
  }
  Outcome apply(std::span<const uint8_t> state, uint8_t, std::span<const uint8_t>, std::span<const uint8_t>& next,
                std::span<char>) override {
    next = state;
    return Outcome::Ok;
  }
  Outcome draw(std::span<const uint8_t>, uint8_t) override {
    ++draws;
    return Outcome::Ok;
  }
  Outcome input(std::span<const uint8_t>, uint8_t, const GameCore::GameEvent&,
                std::span<const uint8_t>& move) override {
    ++inputs;
    move = {&byte, 1};
    return Outcome::Ok;
  }

  int draws = 0;
  int inputs = 0;

 private:
  uint8_t byte = 1;
};

TEST(MatchRoundsNoSeatTest, ATurnThatIsNoSeatIsNeverDrawnOrReadAndNoRoundStarts) {
  NoSeatTurnRules rules;
  GameCore::Session session(GameCore::Roster::pass(2), rules);
  GameTimer timer;
  InputQueue queue;
  MatchRounds rounds(timer, queue);
  ASSERT_EQ(rounds.start(session), Outcome::Ok);
  EXPECT_EQ(rounds.shownSeat(), GameCore::NO_SEAT);
  EXPECT_EQ(rules.draws, 0) << "start's drawShown draws no seat for turn 3";
  ASSERT_EQ(rounds.step(InputEvent{InputKind::Tap, 100, 200}), Outcome::Ok);
  EXPECT_EQ(rules.inputs, 0) << "step delivers the tap to no seat";
  EXPECT_EQ(rules.draws, 0) << "nor draws one after it";
  // A timer due while the turn is no seat (a pass roster, every seat local, so not noSeatTimer's drop) reaches the same
  // guard: delivered to no seat, nothing drawn.
  timer.arm(0, 10);
  GameCore::GameEvent due;
  ASSERT_TRUE(timer.takeDueEvent(10, due));
  ASSERT_EQ(rounds.step(due), Outcome::Ok);
  EXPECT_EQ(rules.inputs, 0) << "step delivers the timer to no seat";
  EXPECT_EQ(rules.draws, 0);
  EXPECT_EQ(rounds.roundsStarted(), 0u);
  EXPECT_EQ(session.ver(), 1u) << "no move was applied";
}

}  // namespace

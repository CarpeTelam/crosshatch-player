#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "IGameRules.h"
#include "Session.h"

using namespace GameCore;

namespace {

// Rules over one-byte states and moves: a state is a counter, a move adds its value,
// and a move of 0xFF is rejected. Every call is recorded.
class FakeRules : public IGameRules {
 public:
  Outcome setup(const GameContext& ctx, std::span<const uint8_t>& state) override {
    calls.push_back("setup");
    seenCtx = ctx;
    out[0] = initial;
    state = {out, 1};
    return Outcome::Ok;
  }
  Outcome status(std::span<const uint8_t> state, const Roster&, Status& result) override {
    calls.push_back("status");
    result = Status{};
    if (state[0] >= overAt) {
      result.over = true;
      result.winners = 1;
    } else {
      result.turn = turn;
    }
    return statusOutcome;
  }
  Outcome apply(std::span<const uint8_t> state, uint8_t seat, std::span<const uint8_t> move,
                std::span<const uint8_t>& next, std::span<char> reason) override {
    calls.push_back("apply");
    appliedSeat = seat;
    if (move[0] == 0xFF) {
      std::snprintf(reason.data(), reason.size(), "%s", "no");
      next = {};
      return applyOutcome;
    }
    out[0] = static_cast<uint8_t>(state[0] + move[0]);
    next = {out, oversizedState ? SNAPSHOT_BYTES + 1 : 1};
    return applyOutcome;
  }
  Outcome draw(std::span<const uint8_t> state, uint8_t seat) override {
    calls.push_back("draw");
    drawnState = state[0];
    drawnSeat = seat;
    return Outcome::Ok;
  }
  Outcome input(std::span<const uint8_t>, uint8_t seat, const GameEvent& event,
                std::span<const uint8_t>& move) override {
    calls.push_back(event.kind == EventKind::Tap        ? "input:tap"
                    : event.kind == EventKind::Rejected ? "input:rejected"
                                                        : "input:over");
    inputs.push_back(calls.back() + "@" + std::to_string(seat));
    if (event.kind == EventKind::Over && seat == failOverSeat) return Outcome::ScriptError;
    if (event.kind == EventKind::Rejected) lastReason = event.reason;
    move = {};
    if (event.kind == EventKind::Tap || answerRuntimeEvents) {
      moveOut[0] = nextMove;
      if (nextMove != 0) move = {moveOut, 1};
    }
    return Outcome::Ok;
  }

  int count(const std::string& call) const {
    int n = 0;
    for (const auto& c : calls) n += c == call;
    return n;
  }

  uint8_t initial = 0;
  uint8_t overAt = 100;
  uint8_t turn = 1;
  uint8_t nextMove = 1;  // 0 returns no move
  bool answerRuntimeEvents = false;
  uint8_t failOverSeat = 0;  // input fails (ScriptError) for an Over event to this seat; 0 for none
  bool oversizedState = false;
  Outcome statusOutcome = Outcome::Ok;
  Outcome applyOutcome = Outcome::Ok;
  GameContext seenCtx;
  uint8_t appliedSeat = 0;
  uint8_t drawnState = 0;
  uint8_t drawnSeat = 0;
  std::string lastReason;
  std::vector<std::string> calls;
  // Each input call as "<call>@<seat>", in order.
  std::vector<std::string> inputs;
  // Oversized states read past `out` only in length; the Session must refuse them first.
  uint8_t out[SNAPSHOT_BYTES + 1] = {};
  uint8_t moveOut[1] = {};
};

GameEvent tap() { return GameEvent{EventKind::Tap, 10, 20}; }

TEST(SessionTest, StartRunsSetupWithTheSoloContextThenStatus) {
  FakeRules rules;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.calls, (std::vector<std::string>{"setup", "status"}));
  EXPECT_EQ(rules.seenCtx.seats, 1);
  EXPECT_EQ(rules.seenCtx.mode, Mode::Solo);
  EXPECT_EQ(rules.seenCtx.api, API_LEVEL);
  EXPECT_EQ(session.ver(), 1u);
  EXPECT_EQ(session.status().turn, 1);
  EXPECT_FALSE(session.pending());
  ASSERT_EQ(session.draw(1), Outcome::Ok);
  EXPECT_EQ(rules.drawnSeat, 1);
}

TEST(SessionTest, AnAcceptedMoveReplacesTheSnapshotAndBumpsVer) {
  FakeRules rules;
  rules.nextMove = 3;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_TRUE(session.pending());
  EXPECT_EQ(rules.count("apply"), 0);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(rules.appliedSeat, 1);
  EXPECT_EQ(session.ver(), 2u);
  ASSERT_EQ(session.snapshot().size(), 1u);
  EXPECT_EQ(session.snapshot()[0], 3);
  EXPECT_EQ(rules.count("status"), 2);  // after setup and after the move
  ASSERT_EQ(session.draw(1), Outcome::Ok);
  EXPECT_EQ(rules.drawnState, 3);
}

TEST(SessionTest, AMoveReturnedWhileOneIsPendingIsDiscarded) {
  FakeRules rules;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);  // input still runs
  EXPECT_EQ(rules.count("input:tap"), 2);
  EXPECT_EQ(session.discardedMoves(), 1u);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);  // nothing left to apply
  EXPECT_EQ(rules.count("apply"), 1);
  EXPECT_EQ(session.snapshot()[0], 1);
}

TEST(SessionTest, ARejectionIsDeliveredAsAnEventAndItsAnswerDiscarded) {
  FakeRules rules;
  rules.nextMove = 0xFF;
  rules.answerRuntimeEvents = true;  // the game answers the rejection with the same move
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(rules.calls.back(), "input:rejected");
  EXPECT_EQ(rules.lastReason, "no");
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(session.discardedMoves(), 1u);
  EXPECT_EQ(session.ver(), 1u);  // no new snapshot
  EXPECT_EQ(session.snapshot()[0], 0);
  EXPECT_EQ(rules.count("status"), 1);
}

TEST(SessionTest, OverIsDeliveredOnceAndLaterMovesAreDiscarded) {
  FakeRules rules;
  rules.overAt = 1;
  rules.answerRuntimeEvents = true;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_TRUE(session.status().over);
  EXPECT_EQ(session.status().winners, 1);
  EXPECT_EQ(rules.count("input:over"), 1);
  EXPECT_EQ(session.discardedMoves(), 1u);  // the answer to over
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
    ASSERT_EQ(session.applyPending(), Outcome::Ok);
  }
  EXPECT_EQ(rules.count("apply"), 1);
  EXPECT_EQ(rules.count("input:over"), 1);
  EXPECT_EQ(session.discardedMoves(), 4u);

  // A rematch starts a new round: ver keeps counting and over comes once more.
  const uint32_t verBefore = session.ver();
  rules.initial = 1;  // over at once
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(session.ver(), verBefore + 1);
  EXPECT_EQ(rules.count("input:over"), 2);
}

TEST(SessionTest, AMoveOffTurnIsDiscarded) {
  FakeRules rules;
  rules.turn = 2;
  Roster roster;
  roster.mode = Mode::Pass;
  roster.seats = 2;
  roster.localSeats = 1;  // this device plays seat 1 only
  Session session(roster, rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.seenCtx.seats, 2);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(session.discardedMoves(), 1u);
}

TEST(SessionTest, RuleFailuresEndTheStepAndClearThePendingMove) {
  FakeRules rules;
  rules.applyOutcome = Outcome::ScriptError;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_EQ(session.applyPending(), Outcome::ScriptError);
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(session.ver(), 1u);

  FakeRules cancelled;
  cancelled.statusOutcome = Outcome::Cancelled;
  Session other(Roster::solo(), cancelled);
  EXPECT_EQ(other.start(), Outcome::Cancelled);
}

TEST(SessionTest, AStateOverTheLimitIsRefused) {
  FakeRules rules;
  rules.oversizedState = true;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_EQ(session.applyPending(), Outcome::ScriptError);
  EXPECT_EQ(session.ver(), 1u);
  EXPECT_EQ(session.snapshot()[0], 0);
}

// ---- the seat-taking Session (epic-pass-and-play entry 1): n = 3, nothing assumes two seats ----

TEST(SessionTest, APassRosterGivesSetupEverySeatAndThePassMode) {
  FakeRules rules;
  Session session(Roster::pass(3), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.seenCtx.seats, 3);
  EXPECT_EQ(rules.seenCtx.mode, Mode::Pass);
  ASSERT_EQ(session.draw(3), Outcome::Ok);
  EXPECT_EQ(rules.drawnSeat, 3);
  ASSERT_EQ(session.draw(0), Outcome::Ok);  // the frame for everyone
  EXPECT_EQ(rules.drawnSeat, 0);
}

TEST(SessionTest, OnlyTheTurnSeatsMoveIsKeptAndItIsAppliedAsThatSeats) {
  FakeRules rules;
  rules.turn = 2;
  Session session(Roster::pass(3), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 3), Outcome::Ok);
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(session.discardedMoves(), 2u);
  ASSERT_EQ(session.handle(tap(), 2), Outcome::Ok);
  EXPECT_TRUE(session.pending());
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:tap@1", "input:tap@3", "input:tap@2"}));
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(rules.count("apply"), 1);
  EXPECT_EQ(rules.appliedSeat, 2);
}

TEST(SessionTest, TheTurnSeatsMoveIsDiscardedWhenThatSeatIsNotLocal) {
  FakeRules rules;
  rules.turn = 2;
  Roster roster;
  roster.mode = Mode::Pass;
  roster.seats = 2;
  roster.localSeats = 0b01;
  Session session(roster, rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 2), Outcome::Ok);
  EXPECT_FALSE(session.pending());
  EXPECT_EQ(session.discardedMoves(), 1u);
}

TEST(SessionTest, OverReachesEachLocalSeatOnceInSeatOrderAndAgainAfterARematch) {
  FakeRules rules;
  rules.overAt = 1;
  rules.answerRuntimeEvents = true;  // every seat answers over with a move
  Session session(Roster::pass(3), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:tap@1", "input:over@1", "input:over@2", "input:over@3"}));
  EXPECT_EQ(session.discardedMoves(), 3u);  // the three answers
  EXPECT_FALSE(session.pending());
  for (const uint8_t seat : {1, 2, 3}) {
    ASSERT_EQ(session.handle(tap(), seat), Outcome::Ok);
    ASSERT_EQ(session.applyPending(), Outcome::Ok);
  }
  EXPECT_EQ(rules.count("apply"), 1);
  EXPECT_EQ(rules.count("input:over"), 3);

  rules.inputs.clear();
  rules.initial = 1;  // the rematch is over at once
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:over@1", "input:over@2", "input:over@3"}));
}

TEST(SessionTest, AFailedOverDeliveryEndsTheStepAndLaterSeatsGetNoOver) {
  FakeRules rules;
  rules.overAt = 1;
  rules.failOverSeat = 2;
  Session session(Roster::pass(3), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_EQ(session.applyPending(), Outcome::ScriptError);
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:tap@1", "input:over@1", "input:over@2"}));
  EXPECT_EQ(rules.count("input:over"), 2) << "seat 3 never gets over after seat 2's failed";
}

TEST(SessionTest, ARosterWithOneLocalSeatGetsOverForThatSeatOnly) {
  FakeRules rules;
  rules.turn = 2;
  rules.overAt = 1;
  Roster roster;  // nearby's shape: two seats, this device plays seat 2
  roster.mode = Mode::Nearby;
  roster.seats = 2;
  roster.localSeats = 0b10;
  Session session(roster, rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 2), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(rules.appliedSeat, 2);
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:tap@2", "input:over@2"}));
}

TEST(SessionTest, ARejectionReachesTheSeatThatMoved) {
  FakeRules rules;
  rules.turn = 2;
  rules.nextMove = 0xFF;
  Session session(Roster::pass(3), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 2), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(rules.inputs, (std::vector<std::string>{"input:tap@2", "input:rejected@2"}));
  EXPECT_EQ(rules.lastReason, "no");
  EXPECT_EQ(session.ver(), 1u);
}

TEST(SessionTest, RosterSeats) {
  const Roster solo = Roster::solo();
  EXPECT_TRUE(solo.isSeat(1));
  EXPECT_FALSE(solo.isSeat(0));
  EXPECT_FALSE(solo.isSeat(2));
  EXPECT_EQ(solo.firstLocalSeat(), 1);
  EXPECT_STREQ(modeName(Mode::Solo), "solo");
  EXPECT_STREQ(modeName(Mode::Nearby), "nearby");
  Roster crowd;
  crowd.seats = 40;  // past the winners mask: seats above MAX_SEATS never count
  EXPECT_TRUE(crowd.isSeat(Roster::MAX_SEATS));
  EXPECT_FALSE(crowd.isSeat(Roster::MAX_SEATS + 1));
  Roster pair;
  pair.seats = 2;
  pair.localSeats = 2;
  EXPECT_EQ(pair.firstLocalSeat(), 2);
  EXPECT_FALSE(pair.isLocal(1));
}

}  // namespace

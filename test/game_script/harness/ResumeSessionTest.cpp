#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "IGameRules.h"
#include "Session.h"
#include "SnapshotMailbox.h"

// Session::restore and the SnapshotMailbox the VM hands its committed snapshots through:
// entry 11 of epic-install-and-launcher. The session half is the same rules the
// game_core SessionTest uses (a one-byte counter), so a restore is seen through the calls
// the rules receive.

namespace {

using namespace GameCore;

// Rules over one-byte states: a state is a counter, a move adds its value, and the round is over at 10.
class CounterRules : public IGameRules {
 public:
  Outcome setup(const GameContext&, std::span<const uint8_t>& state) override {
    calls.push_back("setup");
    out[0] = initial;
    state = {out, 1};
    return Outcome::Ok;
  }
  Outcome status(std::span<const uint8_t> state, const Roster&, Status& result) override {
    calls.push_back("status");
    if (state[0] >= cancelStatusFrom) return Outcome::Cancelled;  // result untouched, as a cancelled call leaves it
    result = Status{};
    if (state[0] >= 10) {
      result.over = true;
      result.winners = 1;
    } else {
      result.turn = 1;
    }
    return Outcome::Ok;
  }
  Outcome apply(std::span<const uint8_t> state, uint8_t, std::span<const uint8_t> move, std::span<const uint8_t>& next,
                std::span<char>) override {
    calls.push_back("apply");
    out[0] = static_cast<uint8_t>(state[0] + move[0]);
    next = {out, 1};
    return Outcome::Ok;
  }
  Outcome draw(std::span<const uint8_t> state, uint8_t) override {
    calls.push_back("draw");
    drawn = state[0];
    return Outcome::Ok;
  }
  Outcome input(std::span<const uint8_t>, uint8_t, const GameEvent& event, std::span<const uint8_t>& move) override {
    calls.push_back(event.kind == EventKind::Tap    ? "input:tap"
                    : event.kind == EventKind::Over ? "input:over"
                                                    : "input");
    move = {};
    if (event.kind == EventKind::Tap) {
      moveOut[0] = 1;
      move = {moveOut, 1};
    }
    return Outcome::Ok;
  }

  int count(const std::string& call) const {
    int n = 0;
    for (const auto& c : calls) n += c == call;
    return n;
  }

  std::vector<std::string> calls;
  uint8_t initial = 1;
  uint8_t cancelStatusFrom = 255;
  uint8_t out[1] = {};
  uint8_t moveOut[1] = {};
  uint8_t drawn = 0;
};

GameEvent tap() { return GameEvent{EventKind::Tap, 10, 20}; }

TEST(ResumeSessionTest, ARestoredSessionStartsFromTheSnapshotWithoutRunningSetup) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {5};
  ASSERT_TRUE(session.restore(saved, 9));
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.count("setup"), 0);
  EXPECT_EQ(rules.calls, (std::vector<std::string>{"status"}));
  EXPECT_EQ(session.ver(), 9u) << "the restored ver is kept, not counted again";
  ASSERT_EQ(session.snapshot().size(), 1u);
  EXPECT_EQ(session.snapshot()[0], 5);
  EXPECT_FALSE(session.status().over);
  EXPECT_EQ(session.status().turn, 1);

  ASSERT_EQ(session.draw(1), Outcome::Ok);
  EXPECT_EQ(rules.drawn, 5);
}

TEST(ResumeSessionTest, TheNextMoveAdvancesFromTheRestoredVer) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {5};
  ASSERT_TRUE(session.restore(saved, 9));
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(session.ver(), 10u);
  EXPECT_EQ(session.snapshot()[0], 6);
}

TEST(ResumeSessionTest, ARematchRunsSetupAndKeepsCountingVer) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {5};
  ASSERT_TRUE(session.restore(saved, 9));
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_EQ(rules.count("setup"), 0);
  ASSERT_EQ(session.start(), Outcome::Ok) << "Play again";
  EXPECT_EQ(rules.count("setup"), 1);
  EXPECT_EQ(session.ver(), 10u);
  EXPECT_EQ(session.snapshot()[0], 1);
}

TEST(ResumeSessionTest, ARestoredSnapshotThatIsOverDeliversOverOnce) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {10};
  ASSERT_TRUE(session.restore(saved, 3));
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_TRUE(session.status().over);
  EXPECT_EQ(rules.count("input:over"), 1);
}

TEST(ResumeSessionTest, ABadSizeIsRefusedAndChangesNothing) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  EXPECT_FALSE(session.restore({}, 4));
  std::vector<uint8_t> big(SNAPSHOT_BYTES + 1, 1);
  EXPECT_FALSE(session.restore(big, 4));
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(rules.count("setup"), 1) << "a refused restore leaves a new match";
  EXPECT_EQ(session.ver(), 1u);
}

TEST(ResumeSessionTest, AnOversizedRestoreDoesNotDisturbAnEarlierGoodOne) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {7};
  ASSERT_TRUE(session.restore(saved, 12));
  std::vector<uint8_t> big(SNAPSHOT_BYTES + 1, 1);
  EXPECT_FALSE(session.restore(big, 99));
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(session.ver(), 12u);
  EXPECT_EQ(session.snapshot()[0], 7);
}

TEST(ResumeSessionTest, ASnapshotAtTheLimitIsRestored) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  std::vector<uint8_t> full(SNAPSHOT_BYTES, 2);
  ASSERT_TRUE(session.restore(full, 1));
  EXPECT_EQ(rules.count("setup"), 0);
  // The rules read only the first byte; the whole copy is what the session holds.
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(session.snapshot().size(), SNAPSHOT_BYTES);
}

TEST(ResumeSessionTest, SettledVerFollowsTheSnapshotsWhoseStatusWasComputed) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  EXPECT_EQ(session.settledVer(), 0u);
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(session.settledVer(), session.ver());
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  ASSERT_EQ(session.applyPending(), Outcome::Ok);
  EXPECT_EQ(session.ver(), 2u);
  EXPECT_EQ(session.settledVer(), 2u);
}

TEST(ResumeSessionTest, ARestoredSnapshotIsSettledByStart) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {5};
  ASSERT_TRUE(session.restore(saved, 9));
  ASSERT_EQ(session.start(), Outcome::Ok);
  EXPECT_EQ(session.ver(), 9u);
  EXPECT_EQ(session.settledVer(), 9u);
}

TEST(ResumeSessionTest, AMoveWhoseStatusIsCancelledIsCommittedButNotSettled) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  ASSERT_EQ(session.start(), Outcome::Ok);
  rules.cancelStatusFrom = 2;
  ASSERT_EQ(session.handle(tap(), 1), Outcome::Ok);
  EXPECT_EQ(session.applyPending(), Outcome::Cancelled);
  EXPECT_EQ(session.ver(), 2u) << "the move was committed";
  EXPECT_EQ(session.settledVer(), 1u) << "but its status never was";
  EXPECT_FALSE(session.status().over) << "and status() is still the previous snapshot's";
}

TEST(ResumeSessionTest, ARestartCancelledAfterSetupLeavesThePreviousStatusAndSettledVer) {
  CounterRules rules;
  Session session(Roster::solo(), rules);
  const uint8_t saved[] = {10};
  ASSERT_TRUE(session.restore(saved, 4));
  ASSERT_EQ(session.start(), Outcome::Ok);
  ASSERT_TRUE(session.status().over);
  rules.initial = 3;
  rules.cancelStatusFrom = 3;
  EXPECT_EQ(session.start(), Outcome::Cancelled) << "Play again, cancelled after setup";
  EXPECT_EQ(session.ver(), 5u);
  EXPECT_EQ(session.settledVer(), 4u);
  EXPECT_TRUE(session.status().over) << "the previous round's status, not the new round's";
}

// ---- the mailbox ----

class MailboxTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> storage = std::vector<uint8_t>(SNAPSHOT_BYTES, 0);
  SnapshotMailbox mailbox{std::span<uint8_t>(storage)};
  std::vector<uint8_t> out = std::vector<uint8_t>(SNAPSHOT_BYTES, 0);
  SnapshotMailbox::Taken taken;
};

TEST_F(MailboxTest, NothingIsPendingUntilASnapshotIsPublished) {
  EXPECT_FALSE(mailbox.pending());
  EXPECT_FALSE(mailbox.take(out, taken));
  mailbox.markPending();
  EXPECT_FALSE(mailbox.pending()) << "nothing was ever published";
  EXPECT_EQ(mailbox.storage().size(), SNAPSHOT_BYTES);
}

TEST_F(MailboxTest, TheLatestPublishedSnapshotWins) {
  const std::vector<uint8_t> a = {1, 2, 3};
  const std::vector<uint8_t> b = {9, 8};
  ASSERT_TRUE(mailbox.publish(a, 1, false));
  ASSERT_TRUE(mailbox.publish(b, 2, true));
  EXPECT_TRUE(mailbox.pending());
  ASSERT_TRUE(mailbox.take(out, taken));
  EXPECT_EQ(taken.length, 2u);
  EXPECT_EQ(taken.ver, 2u);
  EXPECT_TRUE(taken.over);
  EXPECT_EQ(out[0], 9);
  EXPECT_EQ(out[1], 8);
  EXPECT_FALSE(mailbox.pending());
  EXPECT_FALSE(mailbox.take(out, taken)) << "taken once";
}

TEST_F(MailboxTest, MarkPendingPutsTheHeldSnapshotBackAfterAFailedWrite) {
  const std::vector<uint8_t> a = {4, 5, 6};
  ASSERT_TRUE(mailbox.publish(a, 7, false));
  ASSERT_TRUE(mailbox.take(out, taken));
  mailbox.markPending();
  EXPECT_TRUE(mailbox.pending());
  SnapshotMailbox::Taken again;
  ASSERT_TRUE(mailbox.take(out, again));
  EXPECT_EQ(again.ver, 7u);
  EXPECT_EQ(again.length, 3u);
  EXPECT_EQ(out[2], 6);

  // A snapshot published between the take and the mark is the one that comes back.
  EXPECT_FALSE(mailbox.take(out, again));
  ASSERT_TRUE(mailbox.publish(a, 8, false));
  ASSERT_TRUE(mailbox.take(out, taken));
  const std::vector<uint8_t> newer = {1};
  ASSERT_TRUE(mailbox.publish(newer, 9, false));
  mailbox.markPending();
  ASSERT_TRUE(mailbox.take(out, again));
  EXPECT_EQ(again.ver, 9u);
  EXPECT_EQ(again.length, 1u);
}

TEST_F(MailboxTest, AnEmptyOrOversizedSnapshotIsRefusedAndChangesNothing) {
  const std::vector<uint8_t> a = {1, 2};
  ASSERT_TRUE(mailbox.publish(a, 1, false));
  ASSERT_TRUE(mailbox.take(out, taken));
  EXPECT_FALSE(mailbox.publish({}, 2, false));
  const std::vector<uint8_t> big(SNAPSHOT_BYTES + 1, 3);
  EXPECT_FALSE(mailbox.publish(big, 3, false));
  EXPECT_FALSE(mailbox.pending());
  const std::vector<uint8_t> full(SNAPSHOT_BYTES, 4);
  EXPECT_TRUE(mailbox.publish(full, 4, false));
}

TEST_F(MailboxTest, ATakeIntoABufferThatIsTooSmallKeepsTheSnapshotPending) {
  const std::vector<uint8_t> a = {1, 2, 3, 4};
  ASSERT_TRUE(mailbox.publish(a, 1, false));
  std::vector<uint8_t> small(3);
  EXPECT_FALSE(mailbox.take(small, taken));
  EXPECT_TRUE(mailbox.pending());
  EXPECT_TRUE(mailbox.take(out, taken));
}

TEST_F(MailboxTest, ACopyTakenBesideAPublishingThreadIsNeverTorn) {
  // Every snapshot is 200 bytes of one value, that value being its ver's low byte.
  std::atomic<bool> done{false};
  std::thread publisher([&] {
    std::vector<uint8_t> snapshot(200);
    for (uint32_t ver = 1; ver <= 20000; ++ver) {
      std::fill(snapshot.begin(), snapshot.end(), static_cast<uint8_t>(ver));
      mailbox.publish(snapshot, ver, false);
    }
    done.store(true);
  });
  uint32_t lastVer = 0;
  int takes = 0;
  auto takeOne = [&] {
    if (!mailbox.take(out, taken)) return;
    ++takes;
    ASSERT_EQ(taken.length, 200u);
    EXPECT_GE(taken.ver, lastVer) << "a snapshot older than one already taken";
    lastVer = taken.ver;
    for (size_t i = 0; i < taken.length; ++i) {
      ASSERT_EQ(out[i], static_cast<uint8_t>(taken.ver)) << "torn copy at byte " << i << " of ver " << taken.ver;
    }
  };
  while (!done.load()) takeOne();
  publisher.join();
  takeOne();
  EXPECT_GT(takes, 0);
  EXPECT_EQ(lastVer, 20000u) << "the last snapshot is never lost";
}

}  // namespace

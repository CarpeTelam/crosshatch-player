#include <gtest/gtest.h>

#include "EndSlot.h"
#include "MatchSupport.h"
#include "StateQueue.h"

// StateQueue's policy (plan 7, Design Note 4) over ArrayStateStore, and EndSlot's latch.

using namespace GameCore;
using namespace GameCore::MatchTest;

namespace {

Status playing(const uint8_t turn = 2) {
  Status s;
  s.turn = turn;
  return s;
}
Status over() {
  Status s;
  s.over = true;
  s.winners = 1;
  return s;
}

Bytes body(const uint8_t tag, const size_t size = 3) { return Bytes(size, tag); }

struct Rig {
  explicit Rig(const size_t buffers = StateQueue::DEPTH + 1) : store(buffers), queue(store) {}
  StateQueue::Push push(const uint8_t from, const uint16_t ver, const Status& status, const Bytes& snapshot) {
    return queue.push(from, ver, status, snapshot);
  }
  // Takes the oldest, checks its bytes, and releases it.
  bool takeAndCheck(const uint16_t ver, const uint8_t tag) {
    QueuedState q;
    if (!queue.take(q)) return false;
    const std::span<const uint8_t> bytes = queue.bytes(q);
    const bool same = q.ver == ver && !bytes.empty() && bytes[0] == tag && bytes.size() == q.length;
    queue.release(q);
    return same;
  }
  ArrayStateStore store;
  StateQueue queue;
};

TEST(StateQueueTest, AppendsInOrderAndTakesTheOldestFirst) {
  Rig ordered;
  // Distinct seats so nothing is replaced.
  EXPECT_EQ(ordered.push(1, 1, playing(), body(0xA1)), StateQueue::Push::Queued);
  EXPECT_EQ(ordered.push(2, 2, playing(), body(0xA2)), StateQueue::Push::Queued);
  EXPECT_EQ(ordered.push(1, 3, playing(), body(0xA3)), StateQueue::Push::Queued);
  EXPECT_EQ(ordered.queue.size(), 3u);
  EXPECT_TRUE(ordered.takeAndCheck(1, 0xA1));
  EXPECT_TRUE(ordered.takeAndCheck(2, 0xA2));
  EXPECT_TRUE(ordered.takeAndCheck(3, 0xA3));
  QueuedState none;
  EXPECT_FALSE(ordered.queue.take(none));
  EXPECT_EQ(ordered.store.inUse(), 0u);
}

TEST(StateQueueTest, ANewerStateFromTheSameSeatReplacesTheNewestQueuedOne) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 1, playing(), body(0xA1)), StateQueue::Push::Queued);
  EXPECT_EQ(rig.push(1, 2, playing(), body(0xA2, 5)), StateQueue::Push::Replaced);
  EXPECT_EQ(rig.push(1, 3, playing(), body(0xA3, 7)), StateQueue::Push::Replaced);
  EXPECT_EQ(rig.queue.size(), 1u);
  EXPECT_EQ(rig.store.acquired, 1u) << "a replacement reuses the tail's buffer: no second acquire";
  QueuedState q;
  ASSERT_TRUE(rig.queue.take(q));
  EXPECT_EQ(q.ver, 3);
  EXPECT_EQ(q.length, 7);
  EXPECT_EQ(rig.queue.bytes(q).size(), 7u);
  EXPECT_EQ(rig.queue.bytes(q)[0], 0xA3);
  rig.queue.release(q);
}

TEST(StateQueueTest, TheStatusOfTheReplacingStateIsKept) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 1, playing(2), body(1)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(1, 2, playing(1), body(2)), StateQueue::Push::Replaced);
  QueuedState q;
  ASSERT_TRUE(rig.queue.take(q));
  EXPECT_EQ(q.status.turn, 1);
  EXPECT_EQ(q.fromSeat, 1);
  rig.queue.release(q);
}

TEST(StateQueueTest, AQueuedOverStateIsKeptAndTheNewerOneAppendsBehindIt) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 10, over(), body(0xE0)), StateQueue::Push::Queued);
  EXPECT_EQ(rig.push(1, 11, playing(1), body(0xB1)), StateQueue::Push::Queued) << "behind the over, not over it";
  EXPECT_EQ(rig.push(1, 12, playing(2), body(0xB2)), StateQueue::Push::Replaced) << "replaces v11, never the over";
  EXPECT_EQ(rig.queue.size(), 2u);
  EXPECT_TRUE(rig.takeAndCheck(10, 0xE0));
  EXPECT_TRUE(rig.takeAndCheck(12, 0xB2));
}

TEST(StateQueueTest, OnlyTheNewestEntryIsEverReplaced) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 1, playing(), body(0xA1)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(2, 2, playing(), body(0xA2)), StateQueue::Push::Queued) << "another seat: appended";
  // Seat 1 again: the newest entry is seat 2's, so this appends; the older seat-1 entry is not touched.
  EXPECT_EQ(rig.push(1, 3, playing(), body(0xA3)), StateQueue::Push::Queued);
  EXPECT_EQ(rig.queue.size(), 3u);
  EXPECT_TRUE(rig.takeAndCheck(1, 0xA1));
  EXPECT_TRUE(rig.takeAndCheck(2, 0xA2));
  EXPECT_TRUE(rig.takeAndCheck(3, 0xA3));
}

TEST(StateQueueTest, FullWhenTheTailEndsARoundAndIsCountedPerRefusedPush) {
  Rig rig;
  ASSERT_EQ(rig.push(2, 1, playing(), body(1)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(1, 2, over(), body(2)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(1, 3, over(), body(3)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(1, 4, over(), body(4)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.queue.size(), StateQueue::DEPTH);
  EXPECT_EQ(rig.queue.overflows(), 0u);
  EXPECT_EQ(rig.push(1, 5, playing(), body(5)), StateQueue::Push::Full);
  EXPECT_EQ(rig.push(1, 5, playing(), body(5)), StateQueue::Push::Full) << "a resend of the same frame counts again";
  EXPECT_EQ(rig.queue.overflows(), 2u);
  EXPECT_EQ(rig.queue.size(), StateQueue::DEPTH) << "nothing changed";
  // Room again: the same frame is accepted.
  EXPECT_TRUE(rig.takeAndCheck(1, 1));
  EXPECT_EQ(rig.push(1, 5, playing(), body(5)), StateQueue::Push::Queued);
}

TEST(StateQueueTest, FullWhenTheTailIsAnotherSeatsAtDepth) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 1, playing(), body(1)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(2, 2, playing(), body(2)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(1, 3, playing(), body(3)), StateQueue::Push::Queued);
  ASSERT_EQ(rig.push(2, 4, playing(), body(4)), StateQueue::Push::Queued);
  EXPECT_EQ(rig.push(1, 5, playing(), body(5)), StateQueue::Push::Full);
  EXPECT_EQ(rig.push(2, 5, playing(), body(5)), StateQueue::Push::Replaced) << "same seat as the tail still replaces";
}

TEST(StateQueueTest, FullWhenTheStoreHasNoBufferAndNothingLeaks) {
  Rig rig;
  rig.store.denyAcquire = true;
  EXPECT_EQ(rig.push(1, 1, playing(), body(1)), StateQueue::Push::Full);
  EXPECT_EQ(rig.queue.overflows(), 1u);
  EXPECT_EQ(rig.queue.size(), 0u);
  rig.store.denyAcquire = false;
  EXPECT_EQ(rig.push(1, 1, playing(), body(1)), StateQueue::Push::Queued);
  // A store smaller than the depth runs dry before the queue is full.
  Rig small(2);
  EXPECT_FALSE(small.queue.ok());
  EXPECT_EQ(small.push(1, 1, over(), body(1)), StateQueue::Push::Queued);
  EXPECT_EQ(small.push(1, 2, over(), body(2)), StateQueue::Push::Queued);
  EXPECT_EQ(small.push(1, 3, over(), body(3)), StateQueue::Push::Full);
  EXPECT_EQ(small.queue.overflows(), 1u);
  EXPECT_EQ(small.store.inUse(), 2u);
}

TEST(StateQueueTest, InvalidIsEmptyOrOverTheSnapshotLimitAndChangesNothing) {
  Rig rig;
  ASSERT_EQ(rig.push(1, 1, playing(), body(1)), StateQueue::Push::Queued);
  EXPECT_EQ(rig.push(1, 2, playing(), Bytes{}), StateQueue::Push::Invalid);
  EXPECT_EQ(rig.push(1, 2, playing(), Bytes(Protocol::MAX_SNAPSHOT_BYTES + 1, 7)), StateQueue::Push::Invalid);
  EXPECT_EQ(rig.queue.overflows(), 0u) << "an invalid frame is not an overflow";
  EXPECT_EQ(rig.push(1, 2, playing(), Bytes(Protocol::MAX_SNAPSHOT_BYTES, 7)), StateQueue::Push::Replaced);
  QueuedState q;
  ASSERT_TRUE(rig.queue.take(q));
  EXPECT_EQ(q.length, Protocol::MAX_SNAPSHOT_BYTES);
  EXPECT_EQ(rig.queue.bytes(q).size(), Protocol::MAX_SNAPSHOT_BYTES);
  rig.queue.release(q);
}

TEST(StateQueueTest, TakeAndReleaseLeaveNoLeaksAndABufferIsReusedAfterRelease) {
  Rig rig;
  for (int round = 0; round < 20; ++round) {
    for (uint8_t seat = 1; seat <= 4; ++seat) {
      ASSERT_EQ(rig.push(seat, static_cast<uint16_t>(round * 4 + seat), playing(), body(seat)),
                StateQueue::Push::Queued);
    }
    QueuedState q;
    ASSERT_TRUE(rig.queue.take(q));
    EXPECT_EQ(rig.store.inUse(), 4u) << "a taken entry is the caller's until it releases it";
    rig.queue.release(q);
    EXPECT_EQ(q.handle, NO_STATE_HANDLE);
    rig.queue.clear();
    EXPECT_EQ(rig.store.inUse(), 0u);
  }
  EXPECT_EQ(rig.store.badReleases, 0u);
}

TEST(StateQueueTest, ClearAndDestructionReleaseEveryQueuedHandle) {
  ArrayStateStore store;
  {
    StateQueue queue(store);
    ASSERT_EQ(queue.push(1, 1, playing(), body(1)), StateQueue::Push::Queued);
    ASSERT_EQ(queue.push(2, 2, playing(), body(2)), StateQueue::Push::Queued);
    EXPECT_EQ(store.inUse(), 2u);
    queue.clear();
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_EQ(store.inUse(), 0u);
    ASSERT_EQ(queue.push(1, 3, playing(), body(3)), StateQueue::Push::Queued);
    EXPECT_EQ(store.inUse(), 1u);
  }
  EXPECT_EQ(store.inUse(), 0u);
  EXPECT_EQ(store.badReleases, 0u);
}

TEST(StateQueueTest, OkNeedsOneBufferMoreThanTheDepth) {
  ArrayStateStore four(4), five(5);
  EXPECT_FALSE(StateQueue(four).ok());
  EXPECT_TRUE(StateQueue(five).ok());
}

TEST(StateQueueTest, AWrappedRingKeepsItsOrder) {
  Rig rig;
  uint16_t ver = 0;
  for (int i = 0; i < 3; ++i) {
    ++ver;
    ASSERT_EQ(rig.push(1, ver, over(), body(static_cast<uint8_t>(ver))), StateQueue::Push::Queued);
  }
  for (int i = 0; i < 40; ++i) {
    const uint16_t expected = static_cast<uint16_t>(ver - 2);
    ++ver;
    ASSERT_EQ(rig.push(1, ver, over(), body(static_cast<uint8_t>(ver))), StateQueue::Push::Queued);
    ASSERT_TRUE(rig.takeAndCheck(expected, static_cast<uint8_t>(expected))) << i;
  }
}

TEST(EndSlotTest, TheFirstReasonIsLatchedOnceAndLaterOnesAreCountedAndIgnored) {
  EndSlot end;
  Protocol::AbortReason reason = Protocol::AbortReason::Left;
  EXPECT_FALSE(end.latched());
  EXPECT_FALSE(end.peek(reason));
  EXPECT_FALSE(end.take(reason));
  EXPECT_TRUE(end.latch(Protocol::AbortReason::ScriptError));
  EXPECT_FALSE(end.latch(Protocol::AbortReason::Left));
  EXPECT_FALSE(end.latch(Protocol::AbortReason::Timeout));
  EXPECT_EQ(end.ignored(), 2u);
  EXPECT_TRUE(end.latched());
  ASSERT_TRUE(end.peek(reason));
  EXPECT_EQ(reason, Protocol::AbortReason::ScriptError);
  EXPECT_TRUE(end.latched()) << "peek leaves it latched";
}

TEST(EndSlotTest, EveryReasonLatchesAndTakeReturnsItExactlyOnce) {
  for (const Protocol::AbortReason r :
       {Protocol::AbortReason::Left, Protocol::AbortReason::ScriptError, Protocol::AbortReason::Timeout,
        Protocol::AbortReason::Full, Protocol::AbortReason::VersionMismatch}) {
    EndSlot end;
    ASSERT_TRUE(end.latch(r));
    Protocol::AbortReason got = Protocol::AbortReason::Left;
    ASSERT_TRUE(end.take(got));
    EXPECT_EQ(got, r);
    Protocol::AbortReason untouched = Protocol::AbortReason::VersionMismatch;
    EXPECT_FALSE(end.take(untouched)) << "once";
    EXPECT_EQ(untouched, Protocol::AbortReason::VersionMismatch) << "a take that returns false leaves `out` alone";
    EXPECT_TRUE(end.latched()) << "taken is still latched";
    // A later reason stays ignored after the take: the one the match acted on is the one the log shows.
    EXPECT_FALSE(end.latch(Protocol::AbortReason::Timeout));
    EXPECT_FALSE(end.take(got));
    ASSERT_TRUE(end.peek(got));
    EXPECT_EQ(got, r);
  }
}

}  // namespace

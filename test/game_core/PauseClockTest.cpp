#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "IClock.h"
#include "PauseClock.h"

// GameCore::PauseClock: the ledger behind ch.time.ms (play time), one writer (the loop task) and any reader.

using namespace GameCore;

namespace {

class FakeClock : public IClock {
 public:
  uint64_t nowMs() const override { return now.load(std::memory_order_acquire); }
  void advance(const uint64_t ms) { now.fetch_add(ms, std::memory_order_acq_rel); }
  std::atomic<uint64_t> now{1000000};
};

// Read through playMs only: the paused total at the clock's now, and whether the match is paused (play time stands
// still while the clock moves 1 ms; this moves the clock by that 1 ms).
uint64_t pausedTotal(const PauseClock& ledger, const IClock& clock) { return clock.nowMs() - ledger.playMs(clock, 0); }

bool isPaused(const PauseClock& ledger, FakeClock& clock) {
  const uint64_t before = ledger.playMs(clock, 0);
  clock.advance(1);
  return ledger.playMs(clock, 0) == before;
}

TEST(PauseClockTest, NothingPausedIsTheRawElapsedTime) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(5000);
  EXPECT_EQ(ledger.playMs(clock, start), 5000u);
  EXPECT_FALSE(isPaused(ledger, clock));
  EXPECT_EQ(pausedTotal(ledger, clock), 0u);
}

TEST(PauseClockTest, APauseIsLeftOut) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(5000);
  ledger.enter(clock.nowMs());
  clock.advance(30000);
  ledger.leave(clock.nowMs());
  clock.advance(2000);
  EXPECT_EQ(ledger.playMs(clock, start), 7000u);
  EXPECT_EQ(pausedTotal(ledger, clock), 30000u);
}

TEST(PauseClockTest, AReadWhilePausedIsFrozenAtTheValueWhenThePauseBegan) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(5000);
  ledger.enter(clock.nowMs());
  EXPECT_TRUE(isPaused(ledger, clock));
  EXPECT_EQ(ledger.playMs(clock, start), 5000u);
  clock.advance(12345);
  EXPECT_EQ(ledger.playMs(clock, start), 5000u);
  clock.advance(1);
  EXPECT_EQ(ledger.playMs(clock, start), 5000u);
}

TEST(PauseClockTest, TwoPausesBothLeaveTheirIntervalOut) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(1000);
  ledger.enter(clock.nowMs());
  clock.advance(10000);
  ledger.leave(clock.nowMs());
  clock.advance(2000);
  ledger.enter(clock.nowMs());
  EXPECT_EQ(ledger.playMs(clock, start), 3000u);
  clock.advance(500);
  ledger.leave(clock.nowMs());
  clock.advance(100);
  EXPECT_EQ(ledger.playMs(clock, start), 3100u);
  EXPECT_EQ(pausedTotal(ledger, clock), 10500u);
}

TEST(PauseClockTest, EnterTwiceAndLeaveUnpausedAreNoOps) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  ledger.leave(clock.nowMs());  // not paused
  clock.advance(1000);
  ledger.leave(clock.nowMs());
  EXPECT_EQ(ledger.playMs(clock, start), 1000u);
  ledger.enter(clock.nowMs());
  clock.advance(4000);
  ledger.enter(clock.nowMs());  // already paused: the pause keeps its start
  clock.advance(4000);
  ledger.leave(clock.nowMs());
  clock.advance(1000);
  EXPECT_EQ(ledger.playMs(clock, start), 2000u);
}

TEST(PauseClockTest, APauseOfNoTimeChangesNothing) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(700);
  ledger.enter(clock.nowMs());
  ledger.leave(clock.nowMs());
  clock.advance(300);
  EXPECT_EQ(ledger.playMs(clock, start), 1000u);
}

TEST(PauseClockTest, TheStateBitNeverLeaksIntoTheTotalAndALongClockDoesNotWrap) {
  // A clock far past 2^32 ms (49 days) and a pause long in 64 bits: the total stays exact.
  FakeClock clock;
  clock.now = uint64_t{1} << 40;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  clock.advance(uint64_t{1} << 33);
  ledger.enter(clock.nowMs());
  clock.advance(uint64_t{1} << 35);
  EXPECT_EQ(ledger.playMs(clock, start), uint64_t{1} << 33);
  ledger.leave(clock.nowMs());
  clock.advance(10);
  EXPECT_EQ(ledger.playMs(clock, start), (uint64_t{1} << 33) + 10);
  EXPECT_EQ(pausedTotal(ledger, clock), uint64_t{1} << 35);
  EXPECT_EQ(PauseClock::pausedMs(PauseClock::PAUSED_BIT | 5, 12), 7u);
  EXPECT_EQ(PauseClock::pausedMs(5, 1000), 5u);
  EXPECT_EQ(PauseClock::pausedMs(PauseClock::PAUSED_BIT | 50, 10), 0u);  // a clock behind the word clamps
}

TEST(PauseClockTest, AClockBehindTheStartReadsZero) {
  FakeClock clock;
  PauseClock ledger;
  EXPECT_EQ(ledger.playMs(clock, clock.nowMs() + 100), 0u);
}

// The main task enters and leaves while the VM task reads. playMs re-validates its word, so a read is exact for the
// state it validated; the one skew left is the writer's own gap between reading its clock and storing the word, so a
// read that a write overlapped can differ from the exact value by that gap. Hence the upper bound, play time never
// past the raw elapsed time, is checked on every read, and monotonicity only on reads no write overlapped (the writer
// bumps `seq` to odd before it reads its clock and to even after its store, so those reads are exact).
TEST(PauseClockTest, AReaderNeverSeesPlayTimeGoBackwardsOrPastTheRawElapsedTime) {
  FakeClock clock;
  PauseClock ledger;
  const uint64_t start = clock.nowMs();
  std::atomic<bool> done{false};
  std::atomic<bool> readOnce{false};
  std::atomic<uint64_t> seq{0};
  std::atomic<uint64_t> badBackwards{0};
  std::atomic<uint64_t> badPast{0};
  std::atomic<uint64_t> cleanReads{0};

  std::thread reader([&] {
    uint64_t last = 0;
    do {
      const uint64_t seqBefore = seq.load(std::memory_order_seq_cst);
      const uint64_t play = ledger.playMs(clock, start);
      const uint64_t seqAfter = seq.load(std::memory_order_seq_cst);
      const uint64_t after = clock.nowMs();
      readOnce.store(true, std::memory_order_release);
      if (play > after - start) badPast.fetch_add(1);
      if (seqBefore != seqAfter || (seqBefore & 1)) continue;
      cleanReads.fetch_add(1);
      if (play < last) badBackwards.fetch_add(1);
      last = play;
    } while (!done.load(std::memory_order_acquire));
  });

  // Not a starved reader's failure: the writer starts once the reader has read.
  while (!readOnce.load(std::memory_order_acquire)) std::this_thread::yield();

  // Single writer: one thread moves the clock and the ledger (the clock is monotone).
  for (int i = 0; i < 20000; i++) {
    clock.advance(3);
    seq.fetch_add(1, std::memory_order_seq_cst);
    ledger.enter(clock.nowMs());
    seq.fetch_add(1, std::memory_order_seq_cst);
    clock.advance(7);
    seq.fetch_add(1, std::memory_order_seq_cst);
    ledger.leave(clock.nowMs());
    seq.fetch_add(1, std::memory_order_seq_cst);
    clock.advance(2);
  }
  done.store(true, std::memory_order_release);
  reader.join();

  EXPECT_EQ(badBackwards.load(), 0u);
  EXPECT_EQ(badPast.load(), 0u);
  EXPECT_GT(cleanReads.load(), 0u);  // at least the read before the writer began
  EXPECT_EQ(ledger.playMs(clock, start), 20000u * 5);
}

}  // namespace

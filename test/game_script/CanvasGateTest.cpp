#include <gtest/gtest.h>

#include <cstdint>

#include "CanvasGate.h"

namespace {

// A VM-like source that counts its reads: wait() reads seatShownRequest only when the round count did not already hold.
struct FakeVm {
  uint32_t rounds = 1;
  uint32_t seat = 0;
  mutable int roundReads = 0;
  mutable int seatReads = 0;
  uint32_t roundsStarted() const {
    ++roundReads;
    return rounds;
  }
  uint32_t seatShownRequest() const {
    ++seatReads;
    return seat;
  }
};

uint32_t clockNow = 0;
uint32_t fakeClock() { return clockNow; }

TEST(CanvasGateTest, TheGateStartsWaitingForTheFirstRound) {
  CanvasGate gate;
  EXPECT_EQ(gate.roundsAwaited(), 1u);
  EXPECT_EQ(gate.seatAwaited(), 0u);
  FakeVm vm;
  vm.rounds = 0;
  const CanvasGate::Wait w = gate.wait(vm);
  EXPECT_TRUE(w.round);
  EXPECT_TRUE(w.display);
}

TEST(CanvasGateTest, PublishedButNotDisplayedIsAwaitingDisplayOnly) {
  CanvasGate gate;
  FakeVm vm;  // round 1 published
  CanvasGate::Wait w = gate.wait(vm);
  EXPECT_FALSE(w.round);
  EXPECT_TRUE(w.display) << "render has not handed the round's first frame to the panel";
  gate.markDisplayed(3, 1, 0);
  w = gate.wait(vm);
  EXPECT_FALSE(w.round);
  EXPECT_FALSE(w.display);
}

TEST(CanvasGateTest, PlayAgainWaitsForTheNextRoundThenItsDisplay) {
  CanvasGate gate;
  FakeVm vm;
  gate.markDisplayed(3, 1, 0);
  gate.awaitRounds(vm.rounds + 1);
  EXPECT_TRUE(gate.wait(vm).round);
  vm.rounds = 2;
  CanvasGate::Wait w = gate.wait(vm);
  EXPECT_FALSE(w.round);
  EXPECT_TRUE(w.display) << "displayed count still 1";
  gate.markDisplayed(4, 2, 0);
  EXPECT_FALSE(gate.wait(vm).display);
}

TEST(CanvasGateTest, ASeatRequestHoldsUntilTheVmServesThenUntilRenderPushes) {
  CanvasGate gate;
  FakeVm vm;
  gate.markDisplayed(3, 1, 0);
  gate.awaitSeat(2);
  CanvasGate::Wait w = gate.wait(vm);
  EXPECT_TRUE(w.round);
  EXPECT_TRUE(w.display);
  vm.seat = 2;
  w = gate.wait(vm);
  EXPECT_FALSE(w.round);
  EXPECT_TRUE(w.display) << "seatDisplayed still 0";
  gate.markDisplayed(5, 1, 2);
  w = gate.wait(vm);
  EXPECT_FALSE(w.round);
  EXPECT_FALSE(w.display);
}

TEST(CanvasGateTest, WaitShortCircuitsTheSeatReadWhenTheRoundIsAlreadyAwaited) {
  CanvasGate gate;
  FakeVm vm;
  vm.rounds = 0;
  gate.wait(vm);
  EXPECT_EQ(vm.roundReads, 1);
  EXPECT_EQ(vm.seatReads, 0) << "round true already; seatShownRequest is not read";
  FakeVm open;
  gate.wait(open);
  EXPECT_EQ(open.seatReads, 1);
}

TEST(CanvasGateTest, AwaitingARoundIsAwaitingDisplayWhateverRenderHasDisplayed) {
  CanvasGate gate;
  gate.markDisplayed(3, 9, 9);  // the last round's counts are already past the new request
  gate.awaitRounds(2);
  FakeVm vm;  // still round 1
  const CanvasGate::Wait w = gate.wait(vm);
  EXPECT_TRUE(w.round);
  EXPECT_TRUE(w.display);
}

TEST(CanvasGateTest, OpenIsTheRendersGateOnBothCounts) {
  CanvasGate gate;
  EXPECT_FALSE(gate.open(0, 0));
  EXPECT_TRUE(gate.open(1, 0));
  gate.awaitRounds(2);
  EXPECT_FALSE(gate.open(1, 0));
  EXPECT_TRUE(gate.open(2, 0));
  gate.awaitSeat(3);
  EXPECT_FALSE(gate.open(2, 2));
  EXPECT_TRUE(gate.open(2, 3));
  EXPECT_TRUE(gate.open(9, 9));
}

TEST(CanvasGateTest, FrameAtBeforeAnyPushIsTheFrameOnThePanel) {
  CanvasGate gate;
  gate.markDisplayed(5, 1, 0);
  EXPECT_EQ(gate.frameOnPanel(), 5u);
  EXPECT_EQ(gate.frameAt(0), 5u) << "doneMs == 0: no push recorded, whatever the time";
  EXPECT_EQ(gate.frameAt(UINT32_MAX), 5u);
}

TEST(CanvasGateTest, FrameAtAnswersTheFrameBeforeThePushForATouchDownBeforeItCompleted) {
  CanvasGate gate;
  gate.markDisplayed(4, 1, 0);
  clockNow = 1000;
  gate.recordPush(fakeClock);  // records before = 4, done at 1000
  gate.markDisplayed(5, 1, 0);
  EXPECT_EQ(gate.frameAt(999), 4u);
  EXPECT_EQ(gate.frameAt(1000), 5u);
  EXPECT_EQ(gate.frameAt(1001), 5u);
}

TEST(CanvasGateTest, FrameAtComparesAcrossTheClockWrap) {
  CanvasGate gate;
  gate.markDisplayed(4, 1, 0);
  clockNow = 10;  // the clock has wrapped since the touch-down
  gate.recordPush(fakeClock);
  gate.markDisplayed(5, 1, 0);
  EXPECT_EQ(gate.frameAt(UINT32_MAX - 5), 4u) << "a touch-down just before the wrap precedes the push";
  EXPECT_EQ(gate.frameAt(11), 5u);
}

TEST(CanvasGateTest, RecordPushNamesTheFrameOnThePanelBeforeItAndTheClockTime) {
  CanvasGate gate;
  gate.markDisplayed(7, 1, 0);
  clockNow = 50;
  gate.recordPush(fakeClock);
  EXPECT_EQ(gate.frameAt(49), 7u);
  gate.markDisplayed(8, 1, 0);
  EXPECT_EQ(gate.frameAt(49), 7u);
  EXPECT_EQ(gate.frameAt(50), 8u);
}

}  // namespace

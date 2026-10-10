#include <gtest/gtest.h>

#include "TouchLatch.h"

namespace {

TEST(TouchLatchTest, StartsFreeWithNoFingerDown) {
  TouchLatch latch;
  EXPECT_FALSE(latch.latched());
}

TEST(TouchLatchTest, LatchKeepsTheFrameTheTimeAndTheFirstSample) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  EXPECT_TRUE(latch.latched());
  EXPECT_EQ(latch.frame(), 7u);
  EXPECT_EQ(latch.ms(), 1234u);
  EXPECT_EQ(latch.firstX(), 10);
  EXPECT_EQ(latch.firstY(), 20);
}

TEST(TouchLatchTest, SamplesMoveTheLastPointNeverTheFirst) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  latch.sample(10, 20);
  latch.sample(40, 50);
  EXPECT_EQ(latch.firstX(), 10);
  EXPECT_EQ(latch.firstY(), 20);
  EXPECT_EQ(latch.lastX(), 40);
  EXPECT_EQ(latch.lastY(), 50);
}

TEST(TouchLatchTest, AFingerStillDownKeepsTheLatch) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  latch.setHeld(true);
  latch.endIfNotHeld();
  EXPECT_TRUE(latch.latched());
}

TEST(TouchLatchTest, NoFingerDownEndsTheContact) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  latch.setHeld(false);
  latch.endIfNotHeld();
  EXPECT_FALSE(latch.latched());
}

TEST(TouchLatchTest, FreeEndsTheContactWhateverIsHeld) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  latch.setHeld(true);
  latch.free();
  EXPECT_FALSE(latch.latched());
}

TEST(TouchLatchTest, TheNextContactLatchesItsOwnFrame) {
  TouchLatch latch;
  latch.latch(7, 1234, 10, 20);
  latch.free();
  latch.latch(9, 2000, 30, 40);
  EXPECT_EQ(latch.frame(), 9u);
  EXPECT_EQ(latch.ms(), 2000u);
  EXPECT_EQ(latch.firstX(), 30);
}

}  // namespace

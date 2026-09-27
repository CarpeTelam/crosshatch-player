#include <gtest/gtest.h>

#include "GameTimer.h"

using GameCore::EventKind;
using GameCore::GameEvent;
using GameScript::GameTimer;

namespace {

GameEvent timerEvent(uint32_t serial) {
  GameEvent event;
  event.kind = EventKind::Timer;
  event.serial = serial;
  return event;
}

TEST(GameTimerTest, FiresOnceWhenDue) {
  GameTimer timer;
  uint32_t serial = 0;
  EXPECT_FALSE(timer.takeDue(0, serial));
  timer.arm(100, 1000);
  EXPECT_TRUE(timer.pending());
  EXPECT_FALSE(timer.takeDue(1099, serial));
  EXPECT_TRUE(timer.takeDue(1100, serial));
  EXPECT_FALSE(timer.pending());
  EXPECT_FALSE(timer.takeDue(5000, serial));
  EXPECT_TRUE(timer.accepts(timerEvent(serial)));
}

TEST(GameTimerTest, ANewArmingReplacesThePendingOne) {
  GameTimer timer;
  uint32_t serial = 0;
  timer.arm(0, 5000);
  timer.arm(500, 1500);
  EXPECT_FALSE(timer.takeDue(1999, serial));
  EXPECT_TRUE(timer.takeDue(2000, serial));
  EXPECT_FALSE(timer.takeDue(5000, serial));
}

TEST(GameTimerTest, CancelClearsItAndStalesAFiredEvent) {
  GameTimer timer;
  uint32_t serial = 0;
  timer.arm(0, 1000);
  timer.cancel();
  EXPECT_FALSE(timer.pending());
  EXPECT_FALSE(timer.takeDue(10000, serial));

  timer.arm(0, 1000);
  ASSERT_TRUE(timer.takeDue(1000, serial));
  timer.cancel();
  EXPECT_FALSE(timer.accepts(timerEvent(serial)));

  timer.arm(0, 1000);
  ASSERT_TRUE(timer.takeDue(1000, serial));
  timer.arm(1000, 1000);  // re-armed before the fired event was delivered
  EXPECT_FALSE(timer.accepts(timerEvent(serial)));
}

TEST(GameTimerTest, AcceptsEveryOtherEvent) {
  GameTimer timer;
  timer.arm(0, 1000);
  timer.cancel();
  for (EventKind kind : {EventKind::Tap, EventKind::Rejected, EventKind::Over}) {
    GameEvent event;
    event.kind = kind;
    event.serial = 12345;
    EXPECT_TRUE(timer.accepts(event));
  }
}

}  // namespace

#include <gtest/gtest.h>

#include <GameEvent.h>

#include "GameTouch.h"
#include "GameViewport.h"

using GameCore::EventKind;
using GameCore::GameEvent;
using GameCore::SwipeDir;
using GameTouch::Gesture;
using GameTouch::Kind;

namespace {

// The X4 Pro and Sticky portrait screen and canvas (default bezel insets).
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
const GameViewport VIEWPORT(3, 9, 474, 788);

bool classify(const Gesture& gesture, GameEvent& out) {
  return GameTouch::toEvent(gesture, SCREEN_W, SCREEN_H, VIEWPORT, out);
}

Gesture swipe(const int x, const int y, const int endX, const int endY) {
  return Gesture{Kind::Swipe, x, y, endX, endY};
}

TEST(GameTouchTest, NoGestureIsNoEvent) {
  GameEvent event;
  EXPECT_FALSE(classify(Gesture{}, event));
}

TEST(GameTouchTest, TapsAndLongPressesArriveInCanvasCoordinates) {
  GameEvent event;
  ASSERT_TRUE(classify(Gesture{Kind::Tap, 240, 400}, event));
  EXPECT_EQ(event.kind, EventKind::Tap);
  EXPECT_EQ(event.x, 237);
  EXPECT_EQ(event.y, 391);
  ASSERT_TRUE(classify(Gesture{Kind::LongPress, 3, 9}, event));
  EXPECT_EQ(event.kind, EventKind::LongPress);
  EXPECT_EQ(event.x, 0);
  EXPECT_EQ(event.y, 0);
  EXPECT_EQ(event.dir, SwipeDir::None);
  // The last canvas pixel, and the bezel just past it.
  ASSERT_TRUE(classify(Gesture{Kind::Tap, 476, 796}, event));
  EXPECT_EQ(event.x, 473);
  EXPECT_EQ(event.y, 787);
  EXPECT_FALSE(classify(Gesture{Kind::Tap, 477, 400}, event));
  EXPECT_FALSE(classify(Gesture{Kind::LongPress, 240, 5}, event));
}

TEST(GameTouchTest, SwipesCarryTheirStartAndDirection) {
  GameEvent event;
  ASSERT_TRUE(classify(swipe(400, 400, 150, 420), event));
  EXPECT_EQ(event.kind, EventKind::Swipe);
  EXPECT_EQ(event.x, 397);
  EXPECT_EQ(event.y, 391);
  EXPECT_EQ(event.dir, SwipeDir::Left);
  ASSERT_TRUE(classify(swipe(200, 400, 400, 380), event));
  EXPECT_EQ(event.dir, SwipeDir::Right);
  ASSERT_TRUE(classify(swipe(240, 500, 250, 300), event));
  EXPECT_EQ(event.dir, SwipeDir::Up);
  ASSERT_TRUE(classify(swipe(240, 300, 230, 500), event));
  EXPECT_EQ(event.dir, SwipeDir::Down);
}

TEST(GameTouchTest, TheSystemsEdgeSwipesNeverReachTheGame) {
  GameEvent event;
  event.kind = EventKind::Timer;
  // Back: a right swipe starting in the left 25% (x <= 120).
  EXPECT_FALSE(classify(swipe(20, 400, 300, 410), event));
  EXPECT_FALSE(classify(swipe(120, 400, 400, 400), event));
  // Home: an up swipe starting in the bottom 14% (y >= 688).
  EXPECT_FALSE(classify(swipe(240, 780, 240, 500), event));
  EXPECT_FALSE(classify(swipe(240, 688, 250, 400), event));
  // Menu or the light panel: a down swipe starting in the top 14% (y <= 112).
  EXPECT_FALSE(classify(swipe(240, 20, 240, 400), event));
  EXPECT_FALSE(classify(swipe(240, 112, 230, 500), event));
  EXPECT_EQ(event.kind, EventKind::Timer);  // untouched
}

TEST(GameTouchTest, SwipesInTheEdgeBandsThatAreNotEdgeGesturesStayTheGames) {
  GameEvent event;
  // Just outside the Back band, and a left swipe inside it.
  ASSERT_TRUE(classify(swipe(121, 400, 400, 400), event));
  EXPECT_EQ(event.dir, SwipeDir::Right);
  ASSERT_TRUE(classify(swipe(100, 400, 10, 400), event));
  EXPECT_EQ(event.dir, SwipeDir::Left);
  // From the right edge leftwards: no system gesture.
  ASSERT_TRUE(classify(swipe(470, 400, 200, 400), event));
  EXPECT_EQ(event.dir, SwipeDir::Left);
  // A down swipe from the bottom band and an up swipe from the top band.
  ASSERT_TRUE(classify(swipe(240, 750, 240, 790), event));
  EXPECT_EQ(event.dir, SwipeDir::Down);
  ASSERT_TRUE(classify(swipe(240, 100, 240, 20), event));
  EXPECT_EQ(event.dir, SwipeDir::Up);
  // A horizontal swipe from the bottom band is not Home.
  ASSERT_TRUE(classify(swipe(300, 750, 100, 740), event));
  EXPECT_EQ(event.dir, SwipeDir::Left);
}

TEST(GameTouchTest, SwipesStartingOffTheCanvasAreDropped) {
  GameEvent event;
  // From the right bezel strip leftwards (not an edge gesture, but off the canvas).
  EXPECT_FALSE(classify(swipe(478, 400, 200, 400), event));
}

}  // namespace

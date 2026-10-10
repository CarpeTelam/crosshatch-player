#include <GameEvent.h>
#include <gtest/gtest.h>

#include "GameTouch.h"
#include "GameViewport.h"

using GameCore::EventKind;
using GameCore::GameEvent;
using GameCore::SwipeDir;
using GameTouch::Gesture;
using GameTouch::Kind;
using GameTouch::Outcome;

namespace {

// The portrait screen and the Sticky's canvas (the default bezel insets; the X4 Pro's are {9, 7, 3, 7}).
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

// ---- classify: toEvent says exactly "Sent", and names why not ----

Outcome why(const Gesture& gesture, GameEvent& out) {
  return GameTouch::classify(gesture, SCREEN_W, SCREEN_H, VIEWPORT, out);
}

TEST(GameTouchTest, ClassifyNamesEachReasonAGestureIsNotSent) {
  GameEvent event;
  EXPECT_EQ(why(Gesture{}, event), Outcome::NoGesture);
  EXPECT_EQ(why(Gesture{Kind::Ended, 10, 20, 80, 30, 640}, event), Outcome::Ended);
  EXPECT_EQ(why(Gesture{Kind::Tap, 477, 400}, event), Outcome::OffCanvas);
  EXPECT_EQ(why(Gesture{Kind::LongPress, 240, 5}, event), Outcome::OffCanvas);
  EXPECT_EQ(why(swipe(478, 400, 200, 400), event), Outcome::OffCanvas);
  EXPECT_EQ(why(swipe(240, 5, 240, 300), event), Outcome::SystemEdge);
  // The SDK names a direction for every swipe, a tie going horizontal: Outcome::NoDirection is defensive.
  EXPECT_EQ(why(swipe(240, 400, 240, 400), event), Outcome::Sent);
  EXPECT_EQ(event.dir, SwipeDir::Right);
  EXPECT_EQ(why(Gesture{Kind::Tap, 240, 400}, event), Outcome::Sent);
  EXPECT_EQ(why(Gesture{Kind::LongPress, 240, 400}, event), Outcome::Sent);
  EXPECT_EQ(why(swipe(400, 400, 150, 420), event), Outcome::Sent);
}

TEST(GameTouchTest, AnEdgeSwipeIsRefusedBeforeItsDirectionOrItsStartIsRead) {
  GameEvent event;
  EXPECT_EQ(why(swipe(20, 400, 300, 410), event), Outcome::SystemEdge);
  // Starting on the bezel and an edge gesture: the edge rule comes first.
  EXPECT_EQ(why(swipe(1, 400, 300, 410), event), Outcome::SystemEdge);
}

TEST(GameTouchTest, OnlyASentGestureWritesTheCallersEvent) {
  const Gesture gestures[] = {Gesture{}, Gesture{Kind::Ended, 10, 20, 80, 30, 640}, Gesture{Kind::Tap, 477, 400},
                              swipe(240, 5, 240, 300), Gesture{Kind::Tap, 240, 400}};
  for (const Gesture& gesture : gestures) {
    GameEvent event;
    event.kind = EventKind::Timer;
    event.x = 99;
    const Outcome outcome = why(gesture, event);
    if (outcome != Outcome::Sent) {
      EXPECT_EQ(event.kind, EventKind::Timer);
      EXPECT_EQ(event.x, 99);
    } else {
      EXPECT_EQ(event.kind, EventKind::Tap);
    }
  }
}

TEST(GameTouchTest, ToEventIsExactlyClassifyBeingSent) {
  const Gesture gestures[] = {
      Gesture{},
      Gesture{Kind::Ended, 10, 20, 80, 30, 640},
      Gesture{Kind::Ended, -1, -1, -1, -1},
      Gesture{Kind::Tap, 240, 400, 0, 0, 82},
      Gesture{Kind::Tap, 477, 400},
      Gesture{Kind::Tap, 3, 9},
      Gesture{Kind::Tap, 2, 9},
      Gesture{Kind::LongPress, 240, 400},
      Gesture{Kind::LongPress, 240, 5},
      swipe(400, 400, 150, 420),
      swipe(200, 400, 400, 380),
      swipe(240, 500, 250, 300),
      swipe(240, 300, 230, 500),
      swipe(20, 400, 300, 410),
      swipe(240, 780, 240, 500),
      swipe(240, 20, 240, 400),
      swipe(240, 400, 240, 400),
      swipe(478, 400, 200, 400),
      swipe(121, 400, 400, 400),
  };
  for (const Gesture& gesture : gestures) {
    GameEvent viaToEvent;
    GameEvent viaClassify;
    const bool sent = GameTouch::toEvent(gesture, SCREEN_W, SCREEN_H, VIEWPORT, viaToEvent);
    const Outcome outcome = GameTouch::classify(gesture, SCREEN_W, SCREEN_H, VIEWPORT, viaClassify);
    EXPECT_EQ(sent, outcome == Outcome::Sent)
        << "kind " << static_cast<int>(gesture.kind) << " at " << gesture.x << "," << gesture.y;
    if (sent) {
      EXPECT_EQ(viaToEvent.kind, viaClassify.kind);
      EXPECT_EQ(viaToEvent.x, viaClassify.x);
      EXPECT_EQ(viaToEvent.y, viaClassify.y);
      EXPECT_EQ(viaToEvent.dir, viaClassify.dir);
    }
  }
}

}  // namespace

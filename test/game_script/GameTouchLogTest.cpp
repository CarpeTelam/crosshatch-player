#include <GameEvent.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GameTouch.h"
#include "GameTouchLog.h"
#include "GameViewport.h"

using GameCore::GameEvent;
using GameTouch::Gesture;
using GameTouch::Kind;
using GameTouch::Outcome;

namespace {

// The portrait screen and the Sticky's canvas (the default bezel insets).
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
const GameViewport VIEWPORT(3, 9, 474, 788);

// The line the match logs for `gesture`: classify, then format.
std::string lineFor(const Gesture& gesture) {
  GameEvent event;
  const Outcome outcome = GameTouch::classify(gesture, SCREEN_W, SCREEN_H, VIEWPORT, event);
  return GameTouchLog::line(gesture, outcome, event).text;
}

Gesture tap(const int x, const int y, const int32_t heldMs = -1) { return Gesture{Kind::Tap, x, y, 0, 0, heldMs}; }

Gesture swipe(const int x, const int y, const int endX, const int endY, const int32_t heldMs = -1) {
  return Gesture{Kind::Swipe, x, y, endX, endY, heldMs};
}

Gesture ended(const int x, const int y, const int endX, const int endY, const int32_t heldMs = -1) {
  return Gesture{Kind::Ended, x, y, endX, endY, heldMs};
}

TEST(GameTouchLogTest, ATapNamesItsScreenAndCanvasPointsAndItsHold) {
  EXPECT_EQ(lineFor(tap(240, 400, 82)), "tap screen (240,400) canvas (237,391) held 82 ms");
}

TEST(GameTouchLogTest, ATapWithNoHoldPrintsNoHeld) {
  EXPECT_EQ(lineFor(tap(240, 400)), "tap screen (240,400) canvas (237,391)");
  EXPECT_EQ(lineFor(tap(240, 400, 0)), "tap screen (240,400) canvas (237,391) held 0 ms");
}

TEST(GameTouchLogTest, ALongPressPrintsNoHoldWhateverTheGestureCarries) {
  EXPECT_EQ(lineFor(Gesture{Kind::LongPress, 240, 400}), "long press screen (240,400) canvas (237,391)");
  EXPECT_EQ(lineFor(Gesture{Kind::LongPress, 240, 400, 0, 0, 1234}), "long press screen (240,400) canvas (237,391)")
      << "lastTouchHeldMs is stale at a long press";
}

TEST(GameTouchLogTest, ASwipeNamesItsDirectionItsEndsAndItsCanvasStart) {
  EXPECT_EQ(lineFor(swipe(400, 400, 150, 420, 120)),
            "swipe left screen (400,400)->(150,420) canvas (397,391) held 120 ms");
  EXPECT_EQ(lineFor(swipe(200, 400, 400, 380)), "swipe right screen (200,400)->(400,380) canvas (197,391)");
  EXPECT_EQ(lineFor(swipe(240, 500, 250, 300)), "swipe up screen (240,500)->(250,300) canvas (237,491)");
  EXPECT_EQ(lineFor(swipe(240, 300, 230, 500)), "swipe down screen (240,300)->(230,500) canvas (237,291)");
}

TEST(GameTouchLogTest, ATapOffTheCanvasSaysSo) {
  EXPECT_EQ(lineFor(tap(477, 400, 64)), "tap screen (477,400) held 64 ms: not sent, off the canvas");
  EXPECT_EQ(lineFor(Gesture{Kind::LongPress, 240, 5}), "long press screen (240,5): not sent, off the canvas");
}

TEST(GameTouchLogTest, ASwipeThatStartsOffTheCanvasSaysSo) {
  EXPECT_EQ(lineFor(swipe(478, 400, 200, 400, 70)),
            "swipe left screen (478,400)->(200,400) held 70 ms: not sent, off the canvas");
}

TEST(GameTouchLogTest, ASystemEdgeSwipeSaysSo) {
  EXPECT_EQ(lineFor(swipe(240, 5, 240, 300, 90)),
            "swipe down screen (240,5)->(240,300) held 90 ms: not sent, system edge swipe");
  EXPECT_EQ(lineFor(swipe(20, 400, 300, 410)), "swipe right screen (20,400)->(300,410): not sent, system edge swipe");
  EXPECT_EQ(lineFor(swipe(240, 780, 240, 500)), "swipe up screen (240,780)->(240,500): not sent, system edge swipe");
}

TEST(GameTouchLogTest, ASwipeWithNoDirectionSaysSo) {
  // The SDK names a direction for every swipe (a tie goes horizontal), so classify never gives NoDirection; the
  // formatter still has its words for it, and then claims no direction.
  const Gesture still = swipe(240, 400, 240, 400, 100);
  EXPECT_EQ(lineFor(still), "swipe right screen (240,400)->(240,400) canvas (237,391) held 100 ms");
  EXPECT_EQ(GameTouchLog::line(still, Outcome::NoDirection, GameEvent{}).text,
            std::string("swipe screen (240,400)->(240,400) held 100 ms: not sent, no direction"));
}

TEST(GameTouchLogTest, AContactWithNoGesturePrintsItsFirstAndLastSample) {
  EXPECT_EQ(lineFor(ended(10, 20, 80, 30, 640)),
            "contact ended screen (10,20)->(80,30) held 640 ms: not sent, not a tap, long press or swipe");
}

TEST(GameTouchLogTest, EachUnknownIsLeftOut) {
  // Positions the match never saw.
  EXPECT_EQ(lineFor(ended(-1, -1, -1, -1, 640)),
            "contact ended screen unknown held 640 ms: not sent, not a tap, long press or swipe");
  // A first sample with no last one.
  EXPECT_EQ(lineFor(ended(10, 20, -1, -1, 640)),
            "contact ended screen (10,20)->unknown held 640 ms: not sent, not a tap, long press or swipe");
  // The hold.
  EXPECT_EQ(lineFor(ended(10, 20, 80, 30)),
            "contact ended screen (10,20)->(80,30): not sent, not a tap, long press or swipe");
  EXPECT_EQ(lineFor(ended(-1, -1, -1, -1)), "contact ended screen unknown: not sent, not a tap, long press or swipe");
}

TEST(GameTouchLogTest, NoGestureHasNoLine) { EXPECT_EQ(lineFor(Gesture{}), ""); }

TEST(GameTouchLogTest, ALineThatDoesNotFitIsCutNotOverrun) {
  // Appends past the buffer through the formatter's own append: the text is cut to what fits, NUL-terminated, and keeps
  // its start.
  GameTouchLog::Line line;
  size_t used = 0;
  GameTouchLog::detail::append(line, used, "swipe left screen (400,400)");
  const std::string prefix = line.text;
  const std::string tail(300, 'x');
  GameTouchLog::detail::append(line, used, " %s", tail.c_str());
  EXPECT_EQ(used, sizeof(line.text) - 1);
  EXPECT_EQ(std::strlen(line.text), sizeof(line.text) - 1);
  EXPECT_EQ(line.text[sizeof(line.text) - 1], '\0');
  EXPECT_EQ(std::string(line.text).rfind(prefix, 0), 0u);
  // Once full, a further append changes nothing.
  const std::string full = line.text;
  GameTouchLog::detail::append(line, used, " more");
  EXPECT_EQ(std::string(line.text), full);
  EXPECT_EQ(used, sizeof(line.text) - 1);
}

}  // namespace

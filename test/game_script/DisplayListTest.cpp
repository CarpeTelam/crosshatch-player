#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "DisplayList.h"
#include "FrameBuffers.h"
#include "GameInput.h"

using namespace GameScript;

namespace {

TEST(DisplayListTest, RoundTripsEachCommand) {
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  ASSERT_TRUE(list.appendClear(Color::White));
  ASSERT_TRUE(list.appendRect(-5, 10, 100000, 20, Color::Black, true));
  ASSERT_TRUE(list.appendText(40, 50, "Tracer", 6, TextSize::Large, Color::Black));
  EXPECT_EQ(list.count(), 3);

  auto reader = list.reader();
  DrawCommand c;
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Clear);
  EXPECT_EQ(c.color, Color::White);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Rect);
  EXPECT_EQ(c.x, -5);
  EXPECT_EQ(c.y, 10);
  EXPECT_EQ(c.w, INT16_MAX);  // clamped
  EXPECT_EQ(c.h, 20);
  EXPECT_TRUE(c.filled);
  EXPECT_EQ(c.color, Color::Black);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Text);
  EXPECT_EQ(std::string(c.text), "Tracer");
  EXPECT_EQ(c.textLength, 6);
  EXPECT_EQ(c.size, TextSize::Large);
  EXPECT_FALSE(reader.next(c));

  list.clear();
  EXPECT_EQ(list.count(), 0);
  EXPECT_FALSE(list.reader().next(c));
}

TEST(DisplayListTest, RoundTripsLinesCirclesAndAlignedText) {
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  ASSERT_TRUE(list.appendLine(-1, 2, 70000, -70000, Color::White));
  ASSERT_TRUE(list.appendCircle(10, 20, 30, Color::Light, true));
  ASSERT_TRUE(list.appendCircle(1, 2, -3, Color::Black, false));
  ASSERT_TRUE(list.appendText(5, 6, "Hi", 2, TextSize::Small, Color::White, Align::Right));
  ASSERT_TRUE(list.appendText(7, 8, "Yo", 2, TextSize::Medium, Color::Black));
  // Line 10 B, circles 9 B each, texts 10 B of header plus the bytes and a NUL.
  EXPECT_EQ(list.bytes(), 10u + 9u + 9u + 13u + 13u);

  auto reader = list.reader();
  DrawCommand c;
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Line);
  EXPECT_EQ(c.color, Color::White);
  EXPECT_EQ(c.x, -1);
  EXPECT_EQ(c.y, 2);
  EXPECT_EQ(c.x2, INT16_MAX);  // clamped
  EXPECT_EQ(c.y2, INT16_MIN);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Circle);
  EXPECT_EQ(c.color, Color::Light);
  EXPECT_TRUE(c.filled);
  EXPECT_EQ(c.x, 10);
  EXPECT_EQ(c.y, 20);
  EXPECT_EQ(c.r, 30);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Circle);
  EXPECT_FALSE(c.filled);
  EXPECT_EQ(c.r, -3);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Text);
  EXPECT_EQ(c.align, Align::Right);
  EXPECT_EQ(c.size, TextSize::Small);
  EXPECT_EQ(c.color, Color::White);
  EXPECT_EQ(std::string(c.text), "Hi");
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.align, Align::Left);  // the default
  EXPECT_EQ(std::string(c.text), "Yo");
  EXPECT_FALSE(reader.next(c));
}

TEST(DisplayListTest, TheRefreshRequestKeepsTheLargestAndIsNotACommand) {
  std::vector<uint8_t> storage(1024);
  DisplayList list(storage.data(), storage.size());
  EXPECT_EQ(list.refresh(), Refresh::Fast);
  list.requestRefresh(Refresh::Full);
  list.requestRefresh(Refresh::Half);
  EXPECT_EQ(list.refresh(), Refresh::Full);
  EXPECT_EQ(list.count(), 0);
  EXPECT_EQ(list.bytes(), 0u);
  list.clear();
  EXPECT_EQ(list.refresh(), Refresh::Fast);
  list.requestRefresh(Refresh::Half);
  EXPECT_EQ(list.refresh(), Refresh::Half);
}

TEST(DisplayListTest, RefusesThePastCommandLimit) {
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  for (int i = 0; i < MAX_COMMANDS; ++i) ASSERT_TRUE(list.appendClear(Color::White)) << i;
  EXPECT_FALSE(list.appendClear(Color::White));
  EXPECT_EQ(list.count(), MAX_COMMANDS);
}

TEST(DisplayListTest, RefusesPastTheByteLimit) {
  std::vector<uint8_t> storage(MAX_BYTES * 2);
  DisplayList list(storage.data(), storage.size());  // capped at MAX_BYTES
  const std::string big(10000, 'x');
  ASSERT_TRUE(list.appendText(0, 0, big.data(), big.size(), TextSize::Small, Color::Black));
  ASSERT_TRUE(list.appendText(0, 0, big.data(), big.size(), TextSize::Small, Color::Black));
  ASSERT_TRUE(list.appendText(0, 0, big.data(), big.size(), TextSize::Small, Color::Black));
  const size_t before = list.bytes();
  EXPECT_FALSE(list.appendText(0, 0, big.data(), big.size(), TextSize::Small, Color::Black));
  EXPECT_EQ(list.bytes(), before);
  EXPECT_LE(list.bytes(), MAX_BYTES);
}

TEST(FrameBuffersTest, PublishSwapsAndCountsFrames) {
  std::vector<uint8_t> a(1024), b(1024);
  FrameBuffers frames(a.data(), b.data(), 1024);
  EXPECT_EQ(frames.frameGen(), 0u);
  frames.back().appendClear(Color::Black);
  frames.publish();
  EXPECT_EQ(frames.frameGen(), 1u);
  EXPECT_FALSE(frames.inSwap());
  uint16_t frontCount = 0;
  frames.readFront([&](const DisplayList& front) { frontCount = front.count(); });
  EXPECT_EQ(frontCount, 1);
  // The next back buffer is the other list; filling it leaves the front alone.
  frames.back().clear();
  frames.back().appendClear(Color::White);
  frames.back().appendClear(Color::White);
  frames.readFront([&](const DisplayList& front) { frontCount = front.count(); });
  EXPECT_EQ(frontCount, 1);
  frames.publish();
  frames.readFront([&](const DisplayList& front) { frontCount = front.count(); });
  EXPECT_EQ(frontCount, 2);
  EXPECT_EQ(frames.frameGen(), 2u);
}

TEST(InputQueueTest, KeepsOrderAndDropsTheOldestWhenFull) {
  InputQueue queue;
  InputEvent out;
  EXPECT_FALSE(queue.pop(out));
  for (int i = 0; i < static_cast<int>(INPUT_QUEUE_DEPTH); ++i) {
    EXPECT_FALSE(queue.push(InputEvent{InputKind::Tap, static_cast<int16_t>(i), 0}));
  }
  EXPECT_TRUE(queue.push(InputEvent{InputKind::Tap, 100, 0}));  // drops x = 0
  for (int i = 1; i < static_cast<int>(INPUT_QUEUE_DEPTH); ++i) {
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.x, i);
  }
  ASSERT_TRUE(queue.pop(out));
  EXPECT_EQ(out.x, 100);
  EXPECT_FALSE(queue.pop(out));
}

}  // namespace

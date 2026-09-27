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

TEST(DisplayListTest, EqualCommandsHashEqualAndAnyChangeHashesElsewhere) {
  std::vector<uint8_t> a(1024), b(1024);
  DisplayList first(a.data(), a.size());
  DisplayList second(b.data(), b.size());
  EXPECT_EQ(first.hash(), second.hash());  // both empty
  const auto fill = [](DisplayList& list, const int16_t x) {
    list.clear();
    list.appendClear(Color::White);
    list.appendRect(x, 20, 30, 40, Color::Dark, true);
    list.appendText(5, 6, "hi", 2, TextSize::Large, Color::Black, Align::Center);
  };
  fill(first, 10);
  fill(second, 10);
  EXPECT_EQ(first.hash(), second.hash());
  const uint64_t same = first.hash();
  fill(second, 11);
  EXPECT_NE(second.hash(), same);
  EXPECT_NE(DisplayList().hash(), same);
  // The refresh request is not a command: it does not change the hash.
  fill(second, 10);
  second.requestRefresh(Refresh::Full);
  EXPECT_EQ(second.hash(), same);
}

TEST(FrameBuffersTest, CoalescedFramesKeepTheLargestRefreshRequest) {
  std::vector<uint8_t> a(1024), b(1024);
  FrameBuffers frames(a.data(), b.data(), 1024);
  const auto publish = [&](const Refresh hint, const int16_t x) {
    frames.back().clear();
    frames.back().appendRect(x, 0, 1, 1, Color::Black, true);
    frames.back().requestRefresh(hint);
    frames.publish();
  };
  // Three frames before the render takes one: the "full" in the middle wins,
  // and the frame drawn is the last.
  publish(Refresh::Half, 1);
  publish(Refresh::Full, 2);
  publish(Refresh::Fast, 3);
  Refresh taken = Refresh::Fast;
  int16_t x = 0;
  frames.takeFront([&](const DisplayList& front, const Refresh hint) {
    taken = hint;
    DrawCommand command;
    auto reader = front.reader();
    ASSERT_TRUE(reader.next(command));
    x = command.x;
  });
  EXPECT_EQ(taken, Refresh::Full);
  EXPECT_EQ(x, 3);
  // Taking the front resets the request; the next frame brings only its own.
  publish(Refresh::Fast, 4);
  frames.takeFront([&](const DisplayList&, const Refresh hint) { taken = hint; });
  EXPECT_EQ(taken, Refresh::Fast);
  publish(Refresh::Half, 5);
  frames.takeFront([&](const DisplayList&, const Refresh hint) { taken = hint; });
  EXPECT_EQ(taken, Refresh::Half);
  // No publish in between: a repaint of the same frame asks for nothing more.
  frames.takeFront([&](const DisplayList&, const Refresh hint) { taken = hint; });
  EXPECT_EQ(taken, Refresh::Fast);
  // readFront leaves the pending request alone.
  publish(Refresh::Full, 6);
  frames.readFront([](const DisplayList&) {});
  frames.takeFront([&](const DisplayList&, const Refresh hint) { taken = hint; });
  EXPECT_EQ(taken, Refresh::Full);
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

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

TEST(DisplayListTest, RoundTripsIcons) {
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  ASSERT_TRUE(list.appendIcon(-7, 70000, 3, TextSize::Large, Color::White, IconWeight::Fill));
  EXPECT_EQ(list.count(), 1);
  EXPECT_EQ(list.bytes(), 10u);  // op, color, size, weight, x, y, icon
  ASSERT_TRUE(list.appendIcon(12, 34, 0xFFFF, TextSize::Small, Color::Black, IconWeight::Regular));
  EXPECT_EQ(list.count(), 2);
  EXPECT_EQ(list.bytes(), 20u);
  ASSERT_TRUE(list.appendIcon(1, 2, 5, TextSize::Medium, Color::Black, IconWeight::Regular));
  EXPECT_EQ(list.bytes(), 30u);

  auto reader = list.reader();
  DrawCommand c;
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Icon);
  EXPECT_EQ(c.color, Color::White);
  EXPECT_EQ(c.size, TextSize::Large);
  EXPECT_EQ(c.x, -7);
  EXPECT_EQ(c.y, INT16_MAX);  // clamped
  EXPECT_EQ(c.icon, 3);
  EXPECT_EQ(c.weight, IconWeight::Fill);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Icon);
  EXPECT_EQ(c.color, Color::Black);
  EXPECT_EQ(c.size, TextSize::Small);
  EXPECT_EQ(c.x, 12);
  EXPECT_EQ(c.y, 34);
  EXPECT_EQ(c.icon, 0xFFFF);
  EXPECT_EQ(c.weight, IconWeight::Regular);
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Icon);
  EXPECT_EQ(c.size, TextSize::Medium);
  EXPECT_EQ(c.icon, 5);
  EXPECT_EQ(c.weight, IconWeight::Regular);
  EXPECT_FALSE(reader.next(c));
}

TEST(DisplayListTest, RoundTripsImages) {
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  ASSERT_TRUE(list.appendImage(-70000, 5, 31, Color::White));
  EXPECT_EQ(list.count(), 1);
  EXPECT_EQ(list.bytes(), 8u);  // op, color, x, y, image
  ASSERT_TRUE(list.appendImage(12, 34, 0xFFFF, Color::Black));
  EXPECT_EQ(list.count(), 2);
  EXPECT_EQ(list.bytes(), 16u);

  auto reader = list.reader();
  DrawCommand c;
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Image);
  EXPECT_EQ(c.color, Color::White);
  EXPECT_EQ(c.x, INT16_MIN);  // clamped
  EXPECT_EQ(c.y, 5);
  EXPECT_EQ(c.image, 31);
  EXPECT_EQ(c.icon, 0);  // an image carries no icon
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Image);
  EXPECT_EQ(c.color, Color::Black);
  EXPECT_EQ(c.x, 12);
  EXPECT_EQ(c.y, 34);
  EXPECT_EQ(c.image, 0xFFFF);
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

TEST(DisplayListTest, AnImagePastTheByteLimitIsRefused) {
  // One text command takes 10 header bytes, its text, and a NUL.
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  const std::string text(MAX_BYTES - 7 - 11, 'x');  // leaves 7 bytes, one short of an image's 8
  ASSERT_TRUE(list.appendText(0, 0, text.data(), text.size(), TextSize::Small, Color::Black));
  ASSERT_EQ(list.bytes(), MAX_BYTES - 7);
  EXPECT_FALSE(list.appendImage(0, 0, 0, Color::Black));
  EXPECT_EQ(list.bytes(), MAX_BYTES - 7);
  EXPECT_EQ(list.count(), 1);

  // With exactly 8 bytes left it fits and fills the list.
  list.clear();
  const std::string fits(MAX_BYTES - 8 - 11, 'x');
  ASSERT_TRUE(list.appendText(0, 0, fits.data(), fits.size(), TextSize::Small, Color::Black));
  EXPECT_TRUE(list.appendImage(0, 0, 0, Color::White));
  EXPECT_EQ(list.bytes(), MAX_BYTES);
  EXPECT_EQ(list.count(), 2);
}

TEST(DisplayListTest, AnIconPastTheByteLimitIsRefused) {
  // One text command takes 10 header bytes, its text, and a NUL.
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  const std::string text(MAX_BYTES - 9 - 11, 'x');  // leaves 9 bytes, one short of an icon's 10
  ASSERT_TRUE(list.appendText(0, 0, text.data(), text.size(), TextSize::Small, Color::Black));
  ASSERT_EQ(list.bytes(), MAX_BYTES - 9);
  EXPECT_FALSE(list.appendIcon(0, 0, 0, TextSize::Small, Color::Black, IconWeight::Fill));
  EXPECT_EQ(list.bytes(), MAX_BYTES - 9);
  EXPECT_EQ(list.count(), 1);

  // With exactly 10 bytes left it fits and fills the list.
  list.clear();
  const std::string fits(MAX_BYTES - 10 - 11, 'x');
  ASSERT_TRUE(list.appendText(0, 0, fits.data(), fits.size(), TextSize::Small, Color::Black));
  EXPECT_TRUE(list.appendIcon(0, 0, 0, TextSize::Small, Color::Black, IconWeight::Fill));
  EXPECT_EQ(list.bytes(), MAX_BYTES);
  EXPECT_EQ(list.count(), 2);
  auto reader = list.reader();
  DrawCommand c;
  ASSERT_TRUE(reader.next(c));
  ASSERT_TRUE(reader.next(c));
  EXPECT_EQ(c.op, Op::Icon);
  EXPECT_EQ(c.weight, IconWeight::Fill);
  EXPECT_FALSE(reader.next(c));
}

// The icon and image budget counts each blit's pixels inside the canvas.
TEST(DisplayListTest, ChargeBlitCountsTheVisiblePixelsUpToTheLimit) {
  constexpr int32_t W = 480;
  constexpr int32_t H = 800;
  std::vector<uint8_t> storage(MAX_BYTES);
  DisplayList list(storage.data(), storage.size());
  EXPECT_EQ(list.blitPixels(), 0u);

  // Exactly the limit: 64 large icons at the origin, then one pixel more is refused
  // and charges nothing, though a blit wholly off the canvas still fits.
  for (int i = 0; i < 64; ++i) ASSERT_TRUE(list.chargeBlit(0, 0, 128, 128, W, H)) << i;
  EXPECT_EQ(list.blitPixels(), MAX_BLIT_PIXELS);
  EXPECT_FALSE(list.chargeBlit(W - 1, H - 1, 128, 128, W, H));
  EXPECT_EQ(list.blitPixels(), MAX_BLIT_PIXELS);
  EXPECT_TRUE(list.chargeBlit(W, 0, 128, 128, W, H));
  EXPECT_TRUE(list.chargeBlit(-128, -128, 128, 128, W, H));
  EXPECT_EQ(list.blitPixels(), MAX_BLIT_PIXELS);
  EXPECT_EQ(list.count(), 0);  // charging appends nothing

  // clear() starts the next frame from 0.
  list.clear();
  EXPECT_EQ(list.blitPixels(), 0u);

  // Partly on: only the visible part counts, on each edge.
  ASSERT_TRUE(list.chargeBlit(-30, 0, 100, 60, W, H));  // 70 x 60
  EXPECT_EQ(list.blitPixels(), 70u * 60u);
  list.clear();
  ASSERT_TRUE(list.chargeBlit(W - 10, H - 20, 100, 60, W, H));  // 10 x 20
  EXPECT_EQ(list.blitPixels(), 10u * 20u);
  list.clear();
  ASSERT_TRUE(list.chargeBlit(5, -40, 100, 60, W, H));  // 100 x 20
  EXPECT_EQ(list.blitPixels(), 100u * 20u);
  list.clear();
  // Larger than the canvas: the canvas.
  ASSERT_TRUE(list.chargeBlit(-10, -10, 2000, 2000, W, H));
  EXPECT_EQ(list.blitPixels(), static_cast<uint32_t>(W * H));
  list.clear();

  // Coordinates clamp to int16_t as the append records them, so a blit at
  // -70000 sits at -32768 and one at 70000 at 32767: both off the canvas.
  EXPECT_TRUE(list.chargeBlit(-70000, 0, 480, 800, W, H));
  EXPECT_TRUE(list.chargeBlit(0, 70000, 480, 800, W, H));
  EXPECT_TRUE(list.chargeBlit(INT64_MIN, INT64_MAX, UINT32_MAX, UINT32_MAX, W, H));
  EXPECT_EQ(list.blitPixels(), 0u);
  // At -32768 a blit 32800 wide still reaches x = 0..31.
  ASSERT_TRUE(list.chargeBlit(-70000, 0, 32800, 1, W, H));
  EXPECT_EQ(list.blitPixels(), 32u);
  list.clear();

  // A charge that would pass the limit is refused whole, not in part.
  ASSERT_TRUE(list.chargeBlit(0, 0, W, H, W, H));
  ASSERT_TRUE(list.chargeBlit(0, 0, W, H, W, H));
  EXPECT_FALSE(list.chargeBlit(0, 0, W, H, W, H));
  EXPECT_EQ(list.blitPixels(), 2u * W * H);
  ASSERT_TRUE(list.chargeBlit(0, 0, W, (MAX_BLIT_PIXELS - 2 * W * H) / W, W, H));
  EXPECT_EQ(list.blitPixels(), MAX_BLIT_PIXELS - (MAX_BLIT_PIXELS - 2 * W * H) % W);
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

// A due timer is disarmed when GameVM::pollTimer queues its event, so a burst of
// touches must never evict that event (the retro's R2).
TEST(InputQueueTest, AFullQueueDropsTheOldestTouchNeverTheTimer) {
  InputQueue queue;
  InputEvent timer;
  timer.kind = InputKind::Timer;
  timer.serial = 7;
  EXPECT_FALSE(queue.push(InputEvent{InputKind::Tap, 0, 0}));
  EXPECT_FALSE(queue.push(timer));  // second in line
  for (int i = 1; i < static_cast<int>(INPUT_QUEUE_DEPTH) - 1; ++i) {
    EXPECT_FALSE(queue.push(InputEvent{InputKind::Tap, static_cast<int16_t>(i), 0}));
  }
  // Full: taps 0..6 and the timer. Ten swipes drop taps 0..6, then swipes 100..102.
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.push(InputEvent{InputKind::Swipe, static_cast<int16_t>(100 + i), 0})) << i;
  }
  InputEvent out;
  ASSERT_TRUE(queue.pop(out));
  EXPECT_EQ(out.kind, InputKind::Timer);  // still first, as it came before every kept touch
  EXPECT_EQ(out.serial, 7u);
  for (int i = 103; i < 110; ++i) {
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.kind, InputKind::Swipe);
    EXPECT_EQ(out.x, i);
  }
  EXPECT_FALSE(queue.pop(out));
}

TEST(InputQueueTest, AQueueOfTimersDropsTheOldestTimer) {
  // Only a re-armed timer fires again, so every Timer event but the newest is stale.
  InputQueue queue;
  for (uint32_t serial = 1; serial <= INPUT_QUEUE_DEPTH; ++serial) {
    InputEvent timer;
    timer.kind = InputKind::Timer;
    timer.serial = serial;
    EXPECT_FALSE(queue.push(timer));
  }
  EXPECT_TRUE(queue.push(InputEvent{InputKind::Tap, 5, 5}));
  InputEvent out;
  for (uint32_t serial = 2; serial <= INPUT_QUEUE_DEPTH; ++serial) {
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.kind, InputKind::Timer);
    EXPECT_EQ(out.serial, serial);
  }
  ASSERT_TRUE(queue.pop(out));
  EXPECT_EQ(out.kind, InputKind::Tap);
  EXPECT_FALSE(queue.pop(out));
}

}  // namespace

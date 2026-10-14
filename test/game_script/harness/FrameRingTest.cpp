// FrameRing: the radio-free receive ring (src/games/FrameRing.h). The matrix's ring rows, wrap-around, the maxFrame
// limit, and a two-thread stress test: the producer stands in for the Wi-Fi task's receive callback, the consumer for
// the GameLink task. No double here is more permissive than the device except that the host's threads are not pinned
// to two cores; the stress test asserts only what holds under any interleaving.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

#include "FrameRing.h"

namespace {

constexpr size_t MAC = FrameRing::MAC_BYTES;

std::vector<uint8_t> bytes(const size_t size, const uint8_t seed) {
  std::vector<uint8_t> out(size);
  for (size_t i = 0; i < size; i++) out[i] = static_cast<uint8_t>(seed + i * 5);
  return out;
}

std::vector<uint8_t> macOf(const uint8_t seed) { return bytes(MAC, seed); }

// A ring over a vector the test owns, with a poisoned guard band after the storage.
struct RingFixture {
  RingFixture(const size_t depth, const size_t maxFrame)
      : storage(FrameRing::storageBytes(depth, maxFrame) + GUARD, GUARD_BYTE) {
    EXPECT_TRUE(ring.attach(storage.data(), FrameRing::storageBytes(depth, maxFrame), depth, maxFrame));
  }
  ~RingFixture() { EXPECT_TRUE(guardIntact()) << "the ring wrote past its storage"; }

  bool guardIntact() const {
    for (size_t i = storage.size() - GUARD; i < storage.size(); i++) {
      if (storage[i] != GUARD_BYTE) return false;
    }
    return true;
  }
  bool push(const uint8_t macSeed, const std::vector<uint8_t>& frame) {
    return ring.push(macOf(macSeed).data(), frame.data(), frame.size());
  }
  // Pops into a buffer of `capacity`; false when nothing fits or waits.
  bool pop(std::vector<uint8_t>& mac, std::vector<uint8_t>& frame, const size_t capacity = 4096) {
    std::vector<uint8_t> out(capacity);
    mac.assign(MAC, 0);
    size_t length = 0;
    if (!ring.pop(mac.data(), out, length)) return false;
    frame.assign(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(length));
    return true;
  }

  static constexpr size_t GUARD = 64;
  static constexpr uint8_t GUARD_BYTE = 0xA5;
  std::vector<uint8_t> storage;
  FrameRing ring;
};

}  // namespace

TEST(FrameRingTest, SlotAndStorageSizes) {
  static_assert(FrameRing::slotBytes(1470) == 1480);  // 2 + 6 + 1470 = 1478, padded to 4
  static_assert(FrameRing::slotBytes(1) == 12);       // 9, padded
  static_assert(FrameRing::storageBytes(8, 1470) == 11840);
  EXPECT_EQ(FrameRing::slotBytes(1470) % 4, 0u);
}

TEST(FrameRingTest, AttachRules) {
  std::vector<uint8_t> storage(FrameRing::storageBytes(8, 100), 0);
  FrameRing ring;
  EXPECT_FALSE(ring.attached());
  for (const size_t badDepth : {size_t{0}, size_t{1}, size_t{3}, size_t{6}, size_t{12}}) {
    EXPECT_FALSE(ring.attach(storage.data(), storage.size(), badDepth, 100)) << "depth " << badDepth;
  }
  EXPECT_FALSE(ring.attach(storage.data(), storage.size(), 8, 0));
  EXPECT_FALSE(ring.attach(storage.data(), storage.size(), 8, 65536));
  EXPECT_FALSE(ring.attach(nullptr, storage.size(), 8, 100));
  EXPECT_FALSE(ring.attach(storage.data(), storage.size() - 1, 8, 100)) << "one byte short";
  EXPECT_FALSE(ring.attached()) << "a refused attach must leave the ring unattached";
  EXPECT_TRUE(ring.attach(storage.data(), storage.size(), 8, 100));
  EXPECT_TRUE(ring.attached());
  EXPECT_EQ(ring.depth(), 8u);
  EXPECT_EQ(ring.maxFrame(), 100u);

  // The largest limits attach too (depth is any power of two; maxFrame up to the u16 length).
  std::vector<uint8_t> big(FrameRing::storageBytes(2, 65535), 0);
  FrameRing wide;
  EXPECT_TRUE(wide.attach(big.data(), big.size(), 2, 65535));
}

TEST(FrameRingTest, AnUnattachedOrDetachedRingRefusesAndCountsNothing) {
  FrameRing ring;
  const std::vector<uint8_t> frame = bytes(10, 1);
  EXPECT_FALSE(ring.push(macOf(1).data(), frame.data(), frame.size()));
  EXPECT_FALSE(ring.push(nullptr, nullptr, 0));
  uint8_t out[16];
  size_t length = 0;
  EXPECT_FALSE(ring.pop(nullptr, out, length));
  EXPECT_EQ(ring.rejected(), 0u);
  EXPECT_EQ(ring.droppedFull(), 0u);

  RingFixture f(4, 50);
  ASSERT_TRUE(f.push(1, frame));
  f.ring.detach();
  EXPECT_FALSE(f.push(2, frame));
  EXPECT_FALSE(f.ring.pop(nullptr, out, length));
  EXPECT_EQ(f.ring.rejected(), 0u);
  EXPECT_EQ(f.ring.droppedFull(), 0u);
}

TEST(FrameRingTest, FramesComeOutWholeInOrderWithTheirSendersMac) {
  RingFixture f(8, 1470);
  const std::vector<uint8_t> one = bytes(1, 3);
  const std::vector<uint8_t> big = bytes(1470, 5);
  const std::vector<uint8_t> mid = bytes(300, 8);
  ASSERT_TRUE(f.push(10, one));
  ASSERT_TRUE(f.push(20, big));
  ASSERT_TRUE(f.push(30, mid));

  std::vector<uint8_t> mac, frame;
  ASSERT_TRUE(f.pop(mac, frame));
  EXPECT_EQ(frame, one);
  EXPECT_EQ(mac, macOf(10));
  ASSERT_TRUE(f.pop(mac, frame));
  EXPECT_EQ(frame, big);
  EXPECT_EQ(mac, macOf(20));
  ASSERT_TRUE(f.pop(mac, frame));
  EXPECT_EQ(frame, mid);
  EXPECT_EQ(mac, macOf(30));
  EXPECT_FALSE(f.pop(mac, frame));
  EXPECT_EQ(f.ring.rejected() + f.ring.droppedFull() + f.ring.skippedTooLong(), 0u);
}

TEST(FrameRingTest, PushCopiesTheFrameAndReturns) {
  RingFixture f(4, 64);
  std::vector<uint8_t> frame = bytes(64, 7);
  const std::vector<uint8_t> original = frame;
  std::vector<uint8_t> mac = macOf(9);
  ASSERT_TRUE(f.ring.push(mac.data(), frame.data(), frame.size()));
  std::fill(frame.begin(), frame.end(), uint8_t{0xEE});
  std::fill(mac.begin(), mac.end(), uint8_t{0xEE});
  std::vector<uint8_t> gotMac, got;
  ASSERT_TRUE(f.pop(gotMac, got));
  EXPECT_EQ(got, original);
  EXPECT_EQ(gotMac, macOf(9));
}

TEST(FrameRingTest, AFullRingDropsAndCountsAndKeepsWhatItHolds) {
  RingFixture f(4, 100);
  const size_t lengths[4] = {20, 21, 22, 23};
  for (size_t i = 0; i < 4; i++)
    ASSERT_TRUE(f.push(static_cast<uint8_t>(i), bytes(lengths[i], static_cast<uint8_t>(i))));
  EXPECT_FALSE(f.push(99, bytes(30, 99)));
  EXPECT_EQ(f.ring.droppedFull(), 1u);
  EXPECT_FALSE(f.push(98, bytes(30, 98)));
  EXPECT_EQ(f.ring.droppedFull(), 2u);
  EXPECT_EQ(f.ring.rejected(), 0u);

  std::vector<uint8_t> mac, frame;
  for (size_t i = 0; i < 4; i++) {
    ASSERT_TRUE(f.pop(mac, frame));
    EXPECT_EQ(frame, bytes(lengths[i], static_cast<uint8_t>(i))) << "frame " << i << " was damaged by a refused push";
    EXPECT_EQ(mac, macOf(static_cast<uint8_t>(i)));
  }
  EXPECT_FALSE(f.pop(mac, frame));

  // The room a pop makes is usable at once.
  ASSERT_TRUE(f.push(5, bytes(5, 5)));
  EXPECT_EQ(f.ring.droppedFull(), 2u);
}

TEST(FrameRingTest, ABadFrameIsRefusedCountedAndNeverStored) {
  RingFixture f(4, 100);
  const std::vector<uint8_t> frame = bytes(4096, 1);  // longer than any length pushed, so a bad one reads real bytes
  const std::vector<uint8_t> mac = macOf(1);
  EXPECT_FALSE(f.ring.push(mac.data(), frame.data(), 0));
  EXPECT_EQ(f.ring.rejected(), 1u);
  EXPECT_FALSE(f.ring.push(mac.data(), frame.data(), 101));
  EXPECT_EQ(f.ring.rejected(), 2u);
  EXPECT_FALSE(f.ring.push(mac.data(), frame.data(), 4096));
  EXPECT_EQ(f.ring.rejected(), 3u);
  EXPECT_FALSE(f.ring.push(mac.data(), nullptr, 10));
  EXPECT_EQ(f.ring.rejected(), 4u);
  EXPECT_FALSE(f.ring.push(nullptr, frame.data(), 10));
  EXPECT_EQ(f.ring.rejected(), 5u);
  EXPECT_EQ(f.ring.droppedFull(), 0u);

  std::vector<uint8_t> gotMac, got;
  EXPECT_FALSE(f.pop(gotMac, got)) << "a refused frame must not be stored";

  // maxFrame itself is accepted.
  EXPECT_TRUE(f.ring.push(mac.data(), frame.data(), 100));
  ASSERT_TRUE(f.pop(gotMac, got));
  EXPECT_EQ(got, std::vector<uint8_t>(frame.begin(), frame.begin() + 100));
}

TEST(FrameRingTest, TheMaxFrameLimitProtectsTheNextSlot) {
  // Slots are tight (maxFrame 30), so a longer frame stored anyway would land in the next slot.
  RingFixture f(4, 30);
  ASSERT_TRUE(f.push(1, bytes(30, 1)));
  EXPECT_FALSE(f.push(2, bytes(31, 2)));
  EXPECT_FALSE(f.push(3, bytes(200, 3)));
  ASSERT_TRUE(f.push(4, bytes(30, 4)));
  std::vector<uint8_t> mac, frame;
  ASSERT_TRUE(f.pop(mac, frame));
  EXPECT_EQ(frame, bytes(30, 1));
  ASSERT_TRUE(f.pop(mac, frame));
  EXPECT_EQ(frame, bytes(30, 4));
  EXPECT_EQ(f.ring.rejected(), 2u);
}

TEST(FrameRingTest, AFrameLongerThanTheCallersBufferIsDroppedNeverTruncated) {
  RingFixture f(8, 200);
  ASSERT_TRUE(f.push(1, bytes(100, 4)));
  ASSERT_TRUE(f.push(2, bytes(20, 6)));
  ASSERT_TRUE(f.push(3, bytes(150, 7)));

  std::vector<uint8_t> out(50, 0xAA);
  uint8_t mac[MAC] = {};
  size_t length = 0;
  ASSERT_TRUE(f.ring.pop(mac, out, length));
  EXPECT_EQ(length, 20u) << "the long frame must be skipped, not returned short";
  EXPECT_TRUE(std::equal(out.begin(), out.begin() + 20, bytes(20, 6).begin()));
  EXPECT_EQ(out[20], 0xAA) << "the caller's buffer past the frame must stay untouched";
  EXPECT_EQ(f.ring.skippedTooLong(), 1u);

  // The last frame is long too: pop drops it and reports an empty ring, with `length` untouched.
  length = 77;
  EXPECT_FALSE(f.ring.pop(mac, out, length));
  EXPECT_EQ(length, 77u);
  EXPECT_EQ(f.ring.skippedTooLong(), 2u);
  EXPECT_FALSE(f.ring.pop(mac, out, length)) << "a dropped frame must not come back later";
}

TEST(FrameRingTest, PopCanSkipTheMac) {
  RingFixture f(4, 50);
  ASSERT_TRUE(f.push(1, bytes(9, 1)));
  std::vector<uint8_t> out(50);
  size_t length = 0;
  ASSERT_TRUE(f.ring.pop(nullptr, out, length));
  EXPECT_EQ(length, 9u);
}

TEST(FrameRingTest, WrapAroundKeepsEveryFrameAtAnyFill) {
  RingFixture f(4, 90);
  uint8_t pushed = 0;
  uint8_t popped = 0;
  // Fill levels 0..4 cycled many times, so the slot index wraps through every position.
  for (int round = 0; round < 500; round++) {
    const int burst = 1 + round % 4;
    for (int i = 0; i < burst; i++) {
      ASSERT_TRUE(f.push(pushed, bytes(1 + (pushed * 7) % 90, pushed))) << "round " << round;
      pushed++;
    }
    for (int i = 0; i < burst; i++) {
      std::vector<uint8_t> mac, frame;
      ASSERT_TRUE(f.pop(mac, frame));
      EXPECT_EQ(frame, bytes(1 + (popped * 7) % 90, popped)) << "round " << round;
      EXPECT_EQ(mac, macOf(popped));
      popped++;
    }
  }
  EXPECT_EQ(f.ring.droppedFull(), 0u);
}

TEST(FrameRingTest, ReattachEmptiesTheRingAndTheCounters) {
  RingFixture f(2, 20);
  ASSERT_TRUE(f.push(1, bytes(5, 1)));
  ASSERT_TRUE(f.push(2, bytes(5, 2)));
  EXPECT_FALSE(f.push(3, bytes(5, 3)));
  EXPECT_FALSE(f.push(3, bytes(0, 3)));
  ASSERT_TRUE(f.ring.attach(f.storage.data(), FrameRing::storageBytes(2, 20), 2, 20));
  EXPECT_EQ(f.ring.droppedFull(), 0u);
  EXPECT_EQ(f.ring.rejected(), 0u);
  std::vector<uint8_t> mac, frame;
  EXPECT_FALSE(f.pop(mac, frame));
}

// The producer pushes numbered frames of varying length from one thread (the Wi-Fi task) while the consumer pops on
// another (the GameLink task). Whatever the interleaving: the frames the consumer gets are exactly those the
// producer was told were accepted, in order, byte for byte, and the frames refused as full are counted.
TEST(FrameRingTest, TwoThreadStressNeverTearsOrReordersAFrame) {
  constexpr uint32_t FRAMES = 30000;
  constexpr size_t MAX_FRAME = 200;
  RingFixture f(8, MAX_FRAME);

  const auto frameFor = [](const uint32_t n) {
    std::vector<uint8_t> frame(4 + n % (MAX_FRAME - 3));
    std::memcpy(frame.data(), &n, 4);
    for (size_t i = 4; i < frame.size(); i++) frame[i] = static_cast<uint8_t>(n * 31 + i);
    return frame;
  };
  const auto macFor = [](const uint32_t n) {
    std::vector<uint8_t> mac(MAC);
    for (size_t i = 0; i < MAC; i++) mac[i] = static_cast<uint8_t>(n + i);
    return mac;
  };

  std::atomic<bool> producerDone{false};
  std::vector<uint32_t> accepted;
  accepted.reserve(FRAMES);
  uint32_t refused = 0;

  std::thread producer([&] {
    for (uint32_t n = 0; n < FRAMES; n++) {
      const std::vector<uint8_t> frame = frameFor(n);
      const std::vector<uint8_t> mac = macFor(n);
      if (f.ring.push(mac.data(), frame.data(), frame.size())) {
        accepted.push_back(n);
      } else {
        refused++;
      }
      if (n % 64 == 0) std::this_thread::yield();
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::vector<uint32_t> received;
  bool torn = false;
  bool finishing = false;
  std::vector<uint8_t> out(MAX_FRAME);
  uint8_t mac[MAC];
  for (;;) {
    size_t length = 0;
    if (f.ring.pop(mac, out, length)) {
      uint32_t n = 0;
      if (length < 4) {
        torn = true;
        break;
      }
      std::memcpy(&n, out.data(), 4);
      if (n >= FRAMES ||
          std::vector<uint8_t>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(length)) != frameFor(n) ||
          std::vector<uint8_t>(mac, mac + MAC) != macFor(n)) {
        torn = true;
        break;
      }
      received.push_back(n);
      continue;
    }
    if (finishing) break;
    if (producerDone.load(std::memory_order_acquire)) {
      finishing = true;  // one more pass: every push the producer made is visible now
      continue;
    }
    std::this_thread::yield();
  }
  producer.join();

  EXPECT_FALSE(torn) << "a frame came out damaged";
  EXPECT_EQ(received, accepted) << "the consumer must get exactly the accepted frames, in order";
  EXPECT_EQ(f.ring.droppedFull(), refused);
  EXPECT_EQ(accepted.size() + refused, size_t{FRAMES});
  EXPECT_GT(received.size(), 0u);
  EXPECT_EQ(f.ring.rejected(), 0u);
}

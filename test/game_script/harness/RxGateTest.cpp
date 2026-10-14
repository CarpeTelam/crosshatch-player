// RxGate (src/games/RxGate.h): the handshake between the radio's receive callback on the Wi-Fi task and the shutdown
// that frees the PSRAM block holding the receive ring (deferred-work 6.5, first item).
//
// Test doubles:
// - HeldRing stands in for FrameRing::push on the Wi-Fi task while it copies a frame into the ring: the test can hold
//   the next push, which keeps that callback running, as a slow copy or a preempted Wi-Fi task does on the device. It
//   counts the pushes and stores nothing.
// - The tick source and the wait are the test's: a counter that each wait moves one tick (the device's vTaskDelay(1)
//   between polls of xTaskGetTickCount()), or the host's steady clock for the two-thread run.
// More permissive than the device: the host's threads are not pinned to two cores, and x86 orders more strongly than
// the ESP32-S3 (FrameRingTest says the same; a ThreadSanitizer run is the repo's choice, deferred-work 6.5 item 5). The
// tests assert only what holds under any interleaving.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include "RxGate.h"

namespace {

struct HeldRing {
  // The next push waits inside until release(); later pushes do not.
  void holdNext() {
    std::lock_guard<std::mutex> lock(m);
    holding = true;
  }
  void release() {
    std::lock_guard<std::mutex> lock(m);
    holding = false;
    cv.notify_all();
  }
  bool waitInside() {
    std::unique_lock<std::mutex> lock(m);
    return cv.wait_for(lock, std::chrono::seconds(10), [&] { return inside; });
  }

  bool push(const uint8_t*, const uint8_t*, size_t) {
    std::unique_lock<std::mutex> lock(m);
    if (holding) {
      inside = true;
      cv.notify_all();
      cv.wait(lock, [&] { return !holding; });
      inside = false;
    }
    ++pushes;
    return true;
  }

  std::mutex m;
  std::condition_variable cv;
  bool holding = false;
  bool inside = false;
  std::atomic<int> pushes{0};
};

// esp_now_recv_info_t's one field the callback reads.
struct Info {
  const uint8_t* src_addr;
};

using Gate = RxGateT<HeldRing>;

const uint8_t MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
const uint8_t FRAME[5] = {1, 2, 3, 4, 5};
const Info INFO{MAC};

TEST(RxGateTest, ACallbackPushesAWholeFrameOnlyIntoAnOpenRing) {
  HeldRing ring;
  Gate gate;
  gate.receive(&INFO, FRAME, 5);
  EXPECT_EQ(ring.pushes.load(), 0) << "no ring yet";
  gate.open(ring);
  gate.receive(&INFO, FRAME, 5);
  EXPECT_EQ(ring.pushes.load(), 1);
  const Info noAddress{nullptr};
  gate.receive(static_cast<const Info*>(nullptr), FRAME, 5);
  gate.receive(&noAddress, FRAME, 5);
  gate.receive(&INFO, nullptr, 5);
  gate.receive(&INFO, FRAME, 0);
  gate.receive(&INFO, FRAME, -1);
  EXPECT_EQ(ring.pushes.load(), 1) << "a callback with a missing argument pushes nothing";
  gate.close();
  gate.receive(&INFO, FRAME, 5);
  EXPECT_EQ(ring.pushes.load(), 1) << "a closed ring takes nothing";
}

TEST(RxGateTest, QuiesceWithNoCallbackRunningIsTrueAtOnceAndUnhooksTheRingFirst) {
  HeldRing ring;
  Gate gate;
  gate.open(ring);
  uint32_t ticks = 0;
  int pushesWhileStopping = -1;
  const bool quiet = gate.quiesce(
      [&] {
        // A callback that starts once quiesce has begun sees no ring.
        gate.receive(&INFO, FRAME, 5);
        pushesWhileStopping = ring.pushes.load();
      },
      [&] { return ticks; }, [&] { ++ticks; }, 100);
  EXPECT_TRUE(quiet);
  EXPECT_EQ(ticks, 0u) << "no wait";
  EXPECT_EQ(pushesWhileStopping, 0) << "the ring was still hooked when the callbacks were stopped";
}

TEST(RxGateTest, ACallbackHeldPastTheLimitMakesQuiesceFalseAtTheLimit) {
  HeldRing ring;
  Gate gate;
  gate.open(ring);
  ring.holdNext();
  std::thread wifi([&] { gate.receive(&INFO, FRAME, 5); });
  ASSERT_TRUE(ring.waitInside());
  uint32_t ticks = 1000;
  const bool quiet = gate.quiesce([] {}, [&] { return ticks; }, [&] { ++ticks; }, 100);
  EXPECT_FALSE(quiet) << "the caller would free the block the callback is writing";
  EXPECT_EQ(ticks, 1100u) << "it gives up at the limit, not before or after";
  ring.release();
  wifi.join();
  EXPECT_EQ(ring.pushes.load(), 1);
}

// Two threads: the Wi-Fi task delivers frames without pause; shutdown runs while one push is held. quiesce returns
// true only once that push is over, and nothing is pushed after it returns, however long the callbacks keep coming.
TEST(RxGateTest, NothingIsPushedAfterQuiesceReturnsTrue) {
  HeldRing ring;
  Gate gate;
  gate.open(ring);
  ring.holdNext();
  std::atomic<bool> stop{false};
  std::thread wifi([&] {
    while (!stop.load()) {
      gate.receive(&INFO, FRAME, 5);
      std::this_thread::yield();
    }
  });
  ASSERT_TRUE(ring.waitInside());
  std::atomic<bool> returned{false};
  bool quiet = false;
  int pushesAtReturn = -1;
  std::thread shutdown([&] {
    const auto now = [] {
      return static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count());
    };
    quiet = gate.quiesce([] {}, now, [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }, 10000);
    pushesAtReturn = ring.pushes.load();
    returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(returned.load()) << "quiesce returned while a callback was still pushing";
  ring.release();
  shutdown.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));  // the callbacks keep coming
  stop.store(true);
  wifi.join();
  EXPECT_TRUE(quiet);
  EXPECT_GE(pushesAtReturn, 1) << "the held push, begun before the shutdown, completed";
  EXPECT_EQ(ring.pushes.load(), pushesAtReturn) << "a callback pushed after quiesce returned true";
}

}  // namespace

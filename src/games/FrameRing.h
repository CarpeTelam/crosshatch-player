#pragma once

// The receive path of the nearby-play radio: a single-producer, single-consumer ring of whole frames over memory the
// caller owns. The producer is the radio's receive callback (the Wi-Fi task) and the consumer is the GameLink task,
// so the two indices need no lock: each is written by exactly one side. Nothing here allocates, logs, locks, or
// calls any other code, which is all a receive callback may do; the ring knows no radio, so it tests on the host.
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkRadio.cpp (MIT).
// Copyright (c) 2025 CrossPoint Reader organization; Copyright (c) 2026 Mario Ruiz (CrossPlay).
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

class FrameRing {
 public:
  static constexpr size_t MAC_BYTES = 6;
  static constexpr size_t MAX_FRAME_LIMIT = 65535;  // the slot's length field is a u16

  // One slot: u16 length, the sender's 6 B MAC, the frame, padded to 4.
  static constexpr size_t slotBytes(const size_t maxFrame) { return (2 + MAC_BYTES + maxFrame + 3) & ~size_t{3}; }
  static constexpr size_t storageBytes(const size_t depth, const size_t maxFrame) {
    return depth * slotBytes(maxFrame);
  }

  FrameRing() = default;
  FrameRing(const FrameRing&) = delete;
  FrameRing& operator=(const FrameRing&) = delete;

  // Takes `storage` (at least storageBytes(depth, maxFrame) long; the caller keeps it alive while attached) and
  // empties the ring and its counters. False, and the ring stays as it was, unless `depth` is a power of two of
  // at least 2, `maxFrame` is 1..65,535, and the storage is large enough. Call it before the producer can run.
  bool attach(uint8_t* storage, const size_t storageSize, const size_t depth, const size_t maxFrame) {
    if (storage == nullptr || depth < 2 || (depth & (depth - 1)) != 0 || maxFrame < 1 || maxFrame > MAX_FRAME_LIMIT ||
        storageSize < storageBytes(depth, maxFrame)) {
      return false;
    }
    storage_ = storage;
    depth_ = depth;
    maxFrame_ = maxFrame;
    slotBytes_ = slotBytes(maxFrame);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    droppedFull_.store(0, std::memory_order_relaxed);
    rejected_.store(0, std::memory_order_relaxed);
    skippedTooLong_.store(0, std::memory_order_relaxed);
    return true;
  }
  // Lets go of the storage. Call it only once the producer cannot run any more; push and pop then return false.
  void detach() { storage_ = nullptr; }
  bool attached() const { return storage_ != nullptr; }

  // Producer (the receive callback): copies one frame in and returns. False when the ring is not attached (nothing
  // counted); when `mac` or `data` is null or `length` is 0 or over maxFrame (counted in rejected()); or when the
  // ring is full (counted in droppedFull(); the frames stored stay intact and nothing blocks).
  bool push(const uint8_t* mac, const uint8_t* data, const size_t length) {
    if (storage_ == nullptr) return false;
    if (mac == nullptr || data == nullptr || length == 0 || length > maxFrame_) {
      bump(rejected_);
      return false;
    }
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= depth_) {
      bump(droppedFull_);
      return false;
    }
    uint8_t* const slot = slotAt(tail);
    const uint16_t length16 = static_cast<uint16_t>(length);
    std::memcpy(slot, &length16, sizeof(length16));
    std::memcpy(slot + 2, mac, MAC_BYTES);
    std::memcpy(slot + 2 + MAC_BYTES, data, length);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer: takes the oldest frame that fits `out`, with its sender's MAC (6 B, `mac` may be null to skip it).
  // A frame longer than `out` is dropped, never truncated (counted in skippedTooLong()), and pop goes on to the
  // next one, so false means the ring is empty (or not attached), not that a frame was dropped.
  bool pop(uint8_t* mac, const std::span<uint8_t> out, size_t& length) {
    if (storage_ == nullptr) return false;
    for (;;) {
      const uint32_t head = head_.load(std::memory_order_relaxed);
      if (head == tail_.load(std::memory_order_acquire)) return false;
      const uint8_t* const slot = slotAt(head);
      uint16_t length16 = 0;
      std::memcpy(&length16, slot, sizeof(length16));
      const bool fits = length16 <= out.size();
      if (fits) {
        if (mac != nullptr) std::memcpy(mac, slot + 2, MAC_BYTES);
        std::memcpy(out.data(), slot + 2 + MAC_BYTES, length16);
        length = length16;
      } else {
        bump(skippedTooLong_);
      }
      head_.store(head + 1, std::memory_order_release);  // after the copy: the producer may reuse the slot now
      if (fits) return true;
    }
  }

  // Frames dropped because the ring was full: a resend costs time, never state (a diagnostic).
  uint32_t droppedFull() const { return droppedFull_.load(std::memory_order_relaxed); }
  // Frames refused as null, empty, or longer than maxFrame.
  uint32_t rejected() const { return rejected_.load(std::memory_order_relaxed); }
  // Frames dropped by pop because the caller's buffer was shorter.
  uint32_t skippedTooLong() const { return skippedTooLong_.load(std::memory_order_relaxed); }

  size_t depth() const { return depth_; }
  size_t maxFrame() const { return maxFrame_; }

 private:
  // Each counter has one writer (the producer or the consumer), so a load and a store count it, and no
  // read-modify-write is needed. The ring object lives in PSRAM (EspNowLink.h); the Xtensa S32C1I behind a native
  // read-modify-write does not work on external RAM, which this build handles by routing every atomic through
  // ESP-IDF's locked emulation (CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND=y), so a fetch_add would be correct but would
  // take that lock on the Wi-Fi task for every dropped frame.
  static void bump(std::atomic<uint32_t>& counter) {
    counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
  }

  uint8_t* slotAt(const uint32_t index) const { return storage_ + (index & (depth_ - 1)) * slotBytes_; }

  uint8_t* storage_ = nullptr;
  size_t depth_ = 0;
  size_t maxFrame_ = 0;
  size_t slotBytes_ = 0;
  std::atomic<uint32_t> head_{0};  // the consumer owns it; monotonic, wraps harmlessly
  std::atomic<uint32_t> tail_{0};  // the producer owns it
  std::atomic<uint32_t> droppedFull_{0};
  std::atomic<uint32_t> rejected_{0};
  std::atomic<uint32_t> skippedTooLong_{0};
};

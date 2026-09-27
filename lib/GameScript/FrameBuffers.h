#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "DisplayList.h"

namespace GameScript {

// Front and back display lists (AD-7). The VM task alone writes back() during draw
// and then publish()es, which swaps the two under the frame mutex and bumps
// frameGen. The render task reads the front only inside readFront() or
// takeFront(), so it never sees a half-written frame. Lock order: RenderLock,
// then this mutex.
//
// Frames published between two takeFront() calls are coalesced: only the last is
// drawn, but the refresh it gets is the largest any of them asked for, so an
// intermediate frame's "full" is never lost.
class FrameBuffers {
 public:
  // Two lists of `capacityEach` bytes (capped at MAX_BYTES) over caller storage.
  FrameBuffers(uint8_t* storageA, uint8_t* storageB, size_t capacityEach);
  FrameBuffers(const FrameBuffers&) = delete;
  FrameBuffers& operator=(const FrameBuffers&) = delete;

  // VM task only. publish() folds the back list's refresh request into the
  // pending one, then swaps.
  DisplayList& back() { return lists[backIndex]; }
  void publish();

  // Any task. 0 until the first publish.
  uint32_t frameGen() const { return generation.load(std::memory_order_acquire); }
  // True while publish() holds the mutex (for the VM's abandon path).
  bool inSwap() const { return swapping.load(std::memory_order_acquire); }

  // Calls fn(const DisplayList&) with the front list under the frame mutex.
  template <typename Fn>
  void readFront(Fn&& fn) {
    std::lock_guard<std::mutex> lock(mutex);
    fn(static_cast<const DisplayList&>(lists[1 - backIndex]));
  }

  // Render task: calls fn(const DisplayList&, Refresh) with the front list and
  // the largest refresh request of every frame published since the last
  // takeFront(), under the frame mutex, and resets that request to Fast.
  template <typename Fn>
  void takeFront(Fn&& fn) {
    std::lock_guard<std::mutex> lock(mutex);
    const Refresh hint = pendingHint;
    pendingHint = Refresh::Fast;
    fn(static_cast<const DisplayList&>(lists[1 - backIndex]), hint);
  }

 private:
  DisplayList lists[2];
  int backIndex = 0;                    // written only under mutex, by the VM task
  Refresh pendingHint = Refresh::Fast;  // under mutex
  std::mutex mutex;
  std::atomic<uint32_t> generation{0};
  std::atomic<bool> swapping{false};
};

}  // namespace GameScript

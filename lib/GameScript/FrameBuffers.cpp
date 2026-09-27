#include "FrameBuffers.h"

namespace GameScript {

FrameBuffers::FrameBuffers(uint8_t* storageA, uint8_t* storageB, const size_t capacityEach)
    : lists{DisplayList(storageA, capacityEach), DisplayList(storageB, capacityEach)} {}

void FrameBuffers::publish() {
  swapping.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(mutex);
    backIndex = 1 - backIndex;
    generation.fetch_add(1, std::memory_order_acq_rel);
  }
  swapping.store(false, std::memory_order_release);
}

}  // namespace GameScript

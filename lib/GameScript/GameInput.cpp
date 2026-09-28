#include "GameInput.h"

namespace GameScript {

bool InputQueue::push(const InputEvent& event) {
  std::lock_guard<std::mutex> lock(mutex);
  bool dropped = false;
  if (size == INPUT_QUEUE_DEPTH) {
    // The oldest non-timer event goes; the oldest of all when every one is a timer.
    size_t victim = 0;
    while (victim < size && ring[(head + victim) % INPUT_QUEUE_DEPTH].kind == InputKind::Timer) ++victim;
    if (victim == size) victim = 0;
    for (size_t i = victim; i > 0; --i) {
      ring[(head + i) % INPUT_QUEUE_DEPTH] = ring[(head + i - 1) % INPUT_QUEUE_DEPTH];
    }
    head = (head + 1) % INPUT_QUEUE_DEPTH;
    --size;
    dropped = true;
  }
  ring[(head + size) % INPUT_QUEUE_DEPTH] = event;
  ++size;
  return dropped;
}

bool InputQueue::pop(InputEvent& out) {
  std::lock_guard<std::mutex> lock(mutex);
  if (size == 0) return false;
  out = ring[head];
  head = (head + 1) % INPUT_QUEUE_DEPTH;
  --size;
  return true;
}

void InputQueue::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  head = 0;
  size = 0;
}

}  // namespace GameScript

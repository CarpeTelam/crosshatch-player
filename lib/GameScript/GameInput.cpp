#include "GameInput.h"

namespace GameScript {

bool InputQueue::push(const InputEvent& event) {
  std::lock_guard<std::mutex> lock(mutex);
  bool dropped = false;
  if (size == INPUT_QUEUE_DEPTH) {
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

}  // namespace GameScript

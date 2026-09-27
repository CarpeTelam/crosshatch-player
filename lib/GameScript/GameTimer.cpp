#include "GameTimer.h"

namespace GameScript {

void GameTimer::arm(const uint64_t nowMs, const uint64_t delayMs) {
  std::lock_guard<std::mutex> lock(mutex);
  armed = true;
  dueMs = nowMs + delayMs;
  ++serial;
}

void GameTimer::cancel() {
  std::lock_guard<std::mutex> lock(mutex);
  armed = false;
  ++serial;
}

bool GameTimer::pending() const {
  std::lock_guard<std::mutex> lock(mutex);
  return armed;
}

bool GameTimer::takeDue(const uint64_t nowMs, uint32_t& fired) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!armed || nowMs < dueMs) return false;
  armed = false;
  fired = serial;
  return true;
}

bool GameTimer::accepts(const GameCore::GameEvent& event) const {
  if (event.kind != GameCore::EventKind::Timer) return true;
  std::lock_guard<std::mutex> lock(mutex);
  return event.serial == serial;
}

}  // namespace GameScript

#if FREEINK_CAP_GAMES

#include "GameClock.h"

#if defined(SIMULATOR)
#include <chrono>
#else
#include <esp_timer.h>
#endif

uint64_t GameClock::nowMs() const {
#if defined(SIMULATOR)
  const auto since = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(since).count());
#else
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
#endif
}

#endif  // FREEINK_CAP_GAMES

#if FREEINK_CAP_GAMES

#include "GameRandom.h"

#if defined(SIMULATOR)
#include <unistd.h>

#include <chrono>
#else
#include <esp_random.h>
#endif

uint32_t GameRandom::next32() {
#if defined(SIMULATOR)
  uint32_t value = 0;
  if (getentropy(&value, sizeof(value)) == 0) return value;
  return static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#else
  return esp_random();
#endif
}

#endif  // FREEINK_CAP_GAMES

#if FREEINK_CAP_GAMES

#include "GameHostCaps.h"

#include <ApiLevel.h>

namespace {

#if defined(SIMULATOR)
constexpr bool IS_SIMULATOR = true;
#else
constexpr bool IS_SIMULATOR = false;
#endif
constexpr bool NEARBY = HostCapsValues::NEARBY_BUILT && !IS_SIMULATOR;

}  // namespace

GameCore::HostCaps gameHostCaps() {
  return GameCore::HostCaps{API_LEVEL, API_MIN_LEVEL, HostCapsValues::MAX_SEATS, NEARBY, HostCapsValues::PASS};
}

#endif  // FREEINK_CAP_GAMES

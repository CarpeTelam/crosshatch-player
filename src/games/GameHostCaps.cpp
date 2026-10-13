#if FREEINK_CAP_GAMES

#include "GameHostCaps.h"

#include <ApiLevel.h>

namespace {

#if defined(SIMULATOR)
constexpr bool IS_SIMULATOR = true;
#else
constexpr bool IS_SIMULATOR = false;
#endif
// The simulator plays nearby over UdpLink (entry 16), so it reports nearby on; a device build reports it from
// NEARBY_BUILT, which entry 11 turns on.
constexpr bool NEARBY = HostCapsValues::NEARBY_BUILT || IS_SIMULATOR;

}  // namespace

GameCore::HostCaps gameHostCaps() {
  return GameCore::HostCaps{API_LEVEL, API_MIN_LEVEL, HostCapsValues::MAX_SEATS, NEARBY, HostCapsValues::PASS};
}

#endif  // FREEINK_CAP_GAMES

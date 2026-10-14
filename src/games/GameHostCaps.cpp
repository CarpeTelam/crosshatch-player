#if FREEINK_CAP_GAMES

#include "GameHostCaps.h"

#include <ApiLevel.h>

namespace {

#if defined(SIMULATOR)
constexpr bool IS_SIMULATOR = true;
#else
constexpr bool IS_SIMULATOR = false;
#endif
#if FREEINK_DEVICE_X4PRO
constexpr bool IS_X4PRO = true;
#else
constexpr bool IS_X4PRO = false;
#endif
// Owner's Decision of 2026-10-13 (epic-play-nearby, entry 20): Play Nearby is offered on the X4 Pro only, because no
// Sticky has run it. Lift it by dropping IS_X4PRO from this expression once a Sticky device run has played a round.
// The simulator plays nearby over UdpLink (entry 16), so it reports nearby on whichever board it simulates.
constexpr bool NEARBY = IS_SIMULATOR || (IS_X4PRO && HostCapsValues::NEARBY_BUILT);

}  // namespace

GameCore::HostCaps gameHostCaps() {
  return GameCore::HostCaps{API_LEVEL, API_MIN_LEVEL, HostCapsValues::MAX_SEATS, NEARBY, HostCapsValues::PASS};
}

#endif  // FREEINK_CAP_GAMES

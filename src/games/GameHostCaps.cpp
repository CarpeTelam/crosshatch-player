#if FREEINK_CAP_GAMES

#include "GameHostCaps.h"

#include <ApiLevel.h>

namespace {

constexpr int32_t MAX_SEATS = 2;
// epic-play-nearby sets this; the simulator never has the radio.
constexpr bool NEARBY_BUILT = false;
#if defined(SIMULATOR)
constexpr bool IS_SIMULATOR = true;
#else
constexpr bool IS_SIMULATOR = false;
#endif
constexpr bool NEARBY = NEARBY_BUILT && !IS_SIMULATOR;
// epic-pass-and-play sets this with the seat choice; until then no match can run a pass game, so
// Manifest::check must not offer the mode.
constexpr bool PASS = false;

}  // namespace

GameCore::HostCaps gameHostCaps() { return GameCore::HostCaps{API_LEVEL, API_MIN_LEVEL, MAX_SEATS, NEARBY, PASS}; }

#endif  // FREEINK_CAP_GAMES

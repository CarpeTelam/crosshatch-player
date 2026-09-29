// The scripted gameHostCaps() (HostCapsScript.h).

#if FREEINK_CAP_GAMES

#include <ApiLevel.h>

#include "GameHostCaps.h"
#include "HostCapsScript.h"

namespace hostcaps {

Script& script() {
  static Script instance;
  return instance;
}

}  // namespace hostcaps

GameCore::HostCaps gameHostCaps() {
  constexpr int32_t MAX_SEATS = 2;  // as GameHostCaps.cpp
  return GameCore::HostCaps{API_LEVEL, API_MIN_LEVEL, MAX_SEATS, false, hostcaps::script().pass};
}

#endif  // FREEINK_CAP_GAMES

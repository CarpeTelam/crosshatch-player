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
  const hostcaps::Script& script = hostcaps::script();
  return GameCore::HostCaps{API_LEVEL, script.minApi != 0 ? script.minApi : API_MIN_LEVEL, MAX_SEATS, false,
                            script.pass};
}

#endif  // FREEINK_CAP_GAMES

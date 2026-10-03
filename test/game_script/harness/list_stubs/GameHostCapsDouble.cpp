// The scripted gameHostCaps() (HostCapsScript.h), standing in for the device's gameHostCaps()
// (src/games/GameHostCaps.cpp): the same HostCapsValues, with `pass`, `minApi`, and `maxSeats` scriptable.

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
  const hostcaps::Script& script = hostcaps::script();
  return GameCore::HostCaps{API_LEVEL, script.minApi != 0 ? script.minApi : API_MIN_LEVEL, script.maxSeats,
                            HostCapsValues::NEARBY_BUILT, script.pass};
}

#endif  // FREEINK_CAP_GAMES

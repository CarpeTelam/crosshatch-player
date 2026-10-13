// The scripted NearbyProbe::read() (NearbyProbeScript.h), standing in for the device's reader
// (src/games/NearbyProbe.cpp).

#if FREEINK_CAP_GAMES

#include "NearbyProbe.h"
#include "NearbyProbeScript.h"

namespace nearbyprobe {

Script& script() {
  static Script instance;
  return instance;
}

}  // namespace nearbyprobe

namespace NearbyProbe {

Readings read() {
  nearbyprobe::Script& script = nearbyprobe::script();
  ++script.reads;
  return Readings{script.wifiInUse, script.freeInternalHeapBytes};
}

}  // namespace NearbyProbe

#endif  // FREEINK_CAP_GAMES

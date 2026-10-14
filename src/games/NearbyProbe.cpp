#if FREEINK_CAP_GAMES

#include "NearbyProbe.h"

#include <HalMemory.h>
#if !defined(SIMULATOR)
#include <WiFi.h>
#endif

namespace NearbyProbe {

Readings read() {
#if defined(SIMULATOR)
  // The simulator's link is UDP on loopback (UdpLink, entry 16), which never touches Wi-Fi: Wi-Fi is not in use, so the
  // simulator's web server may run beside it. The heap is the simulator's (CROSSPOINT_SIM_FREE_HEAP sets it), which
  // exercises the low-memory refusal.
  return Readings{false, HalMemory::getInternalHeap().freeBytes};
#else
  // The same test CrossPointWebServerActivity::onExit makes: the mode is off only when nothing has started Wi-Fi.
  return Readings{WiFi.getMode() != WIFI_MODE_NULL, HalMemory::getInternalHeap().freeBytes};
#endif
}

}  // namespace NearbyProbe

#endif  // FREEINK_CAP_GAMES

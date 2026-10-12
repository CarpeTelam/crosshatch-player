#if FREEINK_CAP_GAMES && !defined(SIMULATOR)

#include "NearbyProbe.h"

#include <HalMemory.h>
#include <WiFi.h>

namespace NearbyProbe {

Readings read() {
  // The same test CrossPointWebServerActivity::onExit makes: the mode is off only when nothing has started Wi-Fi.
  return Readings{WiFi.getMode() != WIFI_MODE_NULL, HalMemory::getInternalHeap().freeBytes};
}

}  // namespace NearbyProbe

#endif  // FREEINK_CAP_GAMES && !SIMULATOR

#pragma once

// NearbyProbe::read() for the screens that start Nearby, standing in for the device's reader
// (src/games/NearbyProbe.cpp: WiFi.getMode() != WIFI_MODE_NULL and the free internal heap), which is device code and
// outside the host harness. The default is a host with Wi-Fi off and plenty of heap, so a suite that never scripts it
// sees a probe that passes; a test sets the two readings to see either refusal. NearbyProbeTest pins the decision
// (NearbyProbe::decide), which is the real, shared code; what read() returns on a device is entry 11's check. More
// permissive than the device: the reading is whatever the test says, and it never changes by itself.
//
// The definition of read() is in the test executable's library (NearbyProbeDouble.cpp), where the real one is excluded
// by name (esp_now_link.sources.cmake).

#include <cstddef>
#include <cstdint>

#include "NearbyProbe.h"

namespace nearbyprobe {

struct Script {
  bool wifiInUse = false;
  size_t freeInternalHeapBytes = SIZE_MAX;
  int reads = 0;  // read() calls since reset(), so a test sees that a refusal read the probe once
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace nearbyprobe

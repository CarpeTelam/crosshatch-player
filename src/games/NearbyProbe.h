#pragma once

#include <cstddef>

// The precondition of Play Nearby (epic R8): is Wi-Fi in use, and is internal heap short? The decision is pure, so the
// host tests it at the boundary; read() takes the two readings on the device. The verdict carries both facts and
// picks no message: R8 words one message for each cause and says nothing of both at once, so which one the title
// screen shows is the lobby entry's choice (a low heap reading usually follows Wi-Fi being up, so it will probably
// show Wi-Fi first).
namespace NearbyProbe {

// Below this much free internal heap the radio, its tasks, and the lobby may not fit beside a running reader.
inline constexpr size_t MIN_INTERNAL_HEAP_BYTES = 100 * 1024;

struct Readings {
  bool wifiInUse;                // any Wi-Fi mode but off: the web server, a join, or another link
  size_t freeInternalHeapBytes;  // free bytes of internal RAM now
};

struct Verdict {
  bool wifiInUse;
  bool lowMemory;
  constexpr bool canOpen() const { return !wifiInUse && !lowMemory; }
};

// lowMemory is true only below MIN_INTERNAL_HEAP_BYTES: 102,399 B refuses and 102,400 B passes.
constexpr Verdict decide(const Readings& readings) {
  return Verdict{readings.wifiInUse, readings.freeInternalHeapBytes < MIN_INTERNAL_HEAP_BYTES};
}

// The readings: Wi-Fi mode and free internal heap. Defined in NearbyProbe.cpp: the device's, and in the simulator Wi-Fi
// off and the simulator's heap (its link is UdpLink, which never touches Wi-Fi).
Readings read();

}  // namespace NearbyProbe

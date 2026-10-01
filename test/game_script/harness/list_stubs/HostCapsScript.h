#pragma once

// gameHostCaps() for the screens that list games, standing in for the device's gameHostCaps()
// (src/games/GameHostCaps.cpp): the same answer (API_LEVEL and API_MIN_LEVEL from ApiLevel.h, and HostCapsValues'
// seats, radio, and pass) with the three values a test changes:
//  - `pass`, the capability, on by default as on the device (HostCapsValues::PASS); a test turns it off to see a
//    host that cannot start pass, where Manifest::check leaves a solo and pass game only solo.
//  - `minApi`, the oldest api the host runs. 0 stands for the real one (API_MIN_LEVEL), so a test cannot ask for a
//    minimum of 0 (nothing needs one); a higher value makes the api-1 games "too old".
//  - `maxSeats`, the most seats in one match, HostCapsValues::MAX_SEATS by default. A test sets 1 to reach the pass
//    seat guard (passSeats 0) of a game Manifest::check still lets start pass; a one-seat host is not a device host
//    (every device reports MAX_SEATS), so this is a guard's test and nothing a person can meet.
// GameHostCapsTest pins the real function to HostCapsValues, and ModePickerTest's HostCapsDouble test pins these
// defaults to them. The definition is in the test executable itself (GameHostCapsDouble.cpp), so the linker takes it
// before the library member that defines the same name.

#include <cstdint>

#include "GameHostCaps.h"

namespace hostcaps {

struct Script {
  bool pass = HostCapsValues::PASS;              // HostCaps::pass
  int32_t minApi = 0;                            // HostCaps::minApi; 0 = the real one (ApiLevel.h)
  int32_t maxSeats = HostCapsValues::MAX_SEATS;  // HostCaps::maxSeats
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace hostcaps

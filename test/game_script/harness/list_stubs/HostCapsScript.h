#pragma once

// gameHostCaps() for the screens that list games, in place of src/games/GameHostCaps.cpp: the same answer
// (API_LEVEL and API_MIN_LEVEL from ApiLevel.h, two seats, no radio) with the two values a test changes:
//  - `pass`, the capability. On the real host it is off until epic-pass-and-play, so no game is startable by another
//    mode than solo; with it on, a pass-only game is Ok, which only the launcher's solo-start term keeps from opening.
//  - `minApi`, the oldest api the host runs. 0 stands for the real one (API_MIN_LEVEL), so a test cannot ask for a
//    minimum of 0 (nothing needs one); a higher value makes the api-1 games "too old".
// The real values are pinned by GameHostCapsTest, not here. The definition is in the test executable itself
// (GameHostCapsDouble.cpp), so the linker takes it before the library member that defines the same name.

#include <cstdint>

namespace hostcaps {

struct Script {
  bool pass = false;   // HostCaps::pass
  int32_t minApi = 0;  // HostCaps::minApi; 0 = the real one (ApiLevel.h)
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace hostcaps

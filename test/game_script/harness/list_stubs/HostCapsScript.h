#pragma once

// gameHostCaps() for the screens that list games, in place of src/games/GameHostCaps.cpp: the same answer
// (API_LEVEL and API_MIN_LEVEL from ApiLevel.h, two seats, no radio) with the one capability a test turns on,
// `pass`. On the real host it is off until epic-pass-and-play, so no game can be startable by another mode
// than solo, and the list's solo-mode term cannot be told apart from `check.ok()`; with it on, a pass-only game
// is Ok and only that term keeps it out. The real values are pinned by GameHostCapsTest, not here. The
// definition is in the test executable itself (GameHostCapsDouble.cpp), so the linker takes it before the
// library member that defines the same name.

namespace hostcaps {

struct Script {
  bool pass = false;  // HostCaps::pass
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace hostcaps

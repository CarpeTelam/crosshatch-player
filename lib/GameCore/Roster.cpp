#include "Roster.h"

namespace GameCore {

const char* modeName(const Mode mode) {
  switch (mode) {
    case Mode::Solo:
      return "solo";
    case Mode::Pass:
      return "pass";
    case Mode::Nearby:
      return "nearby";
  }
  return "solo";
}

uint8_t Roster::firstLocalSeat() const {
  for (uint8_t seat = 1; seat <= seats; ++seat) {
    if (isLocal(seat)) return seat;
  }
  return 0;
}

}  // namespace GameCore

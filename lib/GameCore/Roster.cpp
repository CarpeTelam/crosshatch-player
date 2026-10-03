#include "Roster.h"

#include <algorithm>

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

Roster Roster::pass(const uint8_t seats) {
  Roster roster;
  roster.mode = Mode::Pass;
  roster.seats = seats;
  const uint8_t counted = seats < MAX_SEATS ? seats : MAX_SEATS;
  roster.localSeats = static_cast<uint16_t>((1u << counted) - 1u);
  return roster;
}

uint8_t Roster::firstLocalSeat() const {
  for (uint8_t seat = 1; seat <= seats; ++seat) {
    if (isLocal(seat)) return seat;
  }
  return 0;
}

uint8_t Roster::localSeatCount() const {
  uint8_t count = 0;
  for (uint8_t seat = 1; seat <= seats && seat <= MAX_SEATS; ++seat) {
    if (isLocal(seat)) ++count;
  }
  return count;
}

uint8_t passSeats(const int32_t seatsMin, const int32_t seatsMax, const int32_t hostMaxSeats) {
  const int32_t fewest = std::max<int32_t>(2, seatsMin);
  const int32_t most = std::min<int32_t>({seatsMax, hostMaxSeats, Roster::MAX_SEATS});
  return fewest <= most ? static_cast<uint8_t>(fewest) : 0;
}

}  // namespace GameCore

#pragma once

#include <cstdint>

#include "ApiLevel.h"

namespace GameCore {

enum class Mode : uint8_t { Solo, Pass, Nearby };

// The mode's name as ctx.mode and the manifest spell it ("solo", "pass", "nearby").
const char* modeName(Mode mode);

// Who plays one match (AD-11), fixed before setup and kept for rematches. Seats are
// 1..seats; localSeats has bit (seat - 1) set for each seat this device plays.
struct Roster {
  // STATE carries the winners as a 16-bit mask (AD-13), so no match has more seats.
  static constexpr uint8_t MAX_SEATS = 16;

  Mode mode = Mode::Solo;
  uint8_t seats = 1;
  uint16_t localSeats = 1;
  // ctx.api: the lowest API level in the roster (AD-13); this host's in solo.
  int32_t api = API_LEVEL;

  static constexpr Roster solo() { return Roster{}; }
  // One device passed between the players: `seats` seats, every one of them local.
  static Roster pass(uint8_t seats);

  bool isSeat(const int64_t seat) const { return seat >= 1 && seat <= seats && seat <= MAX_SEATS; }
  bool isLocal(const int64_t seat) const { return isSeat(seat) && (localSeats >> (seat - 1) & 1u) != 0; }
  // The lowest local seat: the one a one-seat session draws and reads input for.
  uint8_t firstLocalSeat() const;
  // How many seats this device plays.
  uint8_t localSeatCount() const;
};

// The seats a pass match of a game with seats `seatsMin`..`seatsMax` (the manifest's) has on a
// host of `hostMaxSeats`: the fewest a pass match can have, max(2, seatsMin), when that is
// within min(seatsMax, hostMaxSeats, Roster::MAX_SEATS); 0 when no pass match fits.
uint8_t passSeats(int32_t seatsMin, int32_t seatsMax, int32_t hostMaxSeats);

}  // namespace GameCore

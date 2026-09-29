#pragma once

#include <cstdint>

namespace GameCore {

// What this host can start (spine AD-15). One src/games provider fills it from
// ApiLevel.h and the build; the list, installer, launcher, and lobby all take it
// from there and pass it to Manifest::check.
struct HostCaps {
  int32_t api = 0;       // newest API level (API_LEVEL)
  int32_t minApi = 0;    // oldest API level still run (API_MIN_LEVEL)
  int32_t maxSeats = 0;  // most seats in one match
  bool nearby = false;   // Play Nearby is available
  bool pass = false;     // Pass and Play is available (false until epic-pass-and-play sets it)
};

}  // namespace GameCore

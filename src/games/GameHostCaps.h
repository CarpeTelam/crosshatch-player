#pragma once

#include <HostCaps.h>

#include <cstdint>

// The values gameHostCaps() reports beside ApiLevel.h's, shared with the host suites'
// double (test/game_script/harness/list_stubs/GameHostCapsDouble.cpp), so the two cannot
// drift; GameHostCapsTest pins the real function to them and ModePickerTest the double.
namespace HostCapsValues {
// The most seats in one match (api-level-1.txt's seats_max).
inline constexpr int32_t MAX_SEATS = 2;
// epic-play-nearby sets this; the simulator never has the radio.
inline constexpr bool NEARBY_BUILT = false;
// Pass and Play: an open pass match with the fewest seats it can have (epic-pass-and-play).
inline constexpr bool PASS = true;
}  // namespace HostCapsValues

// The one HostCaps provider (spine AD-15): the API levels from ApiLevel.h and
// HostCapsValues: two seats, Pass and Play on, and Play Nearby off until
// epic-play-nearby turns it on for device builds. The simulator has no radio, so
// nearby stays false there (AD-2).
GameCore::HostCaps gameHostCaps();

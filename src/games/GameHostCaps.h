#pragma once

#include <HostCaps.h>

#include <cstdint>

// The values gameHostCaps() reports beside ApiLevel.h's, shared with the host suites'
// double (test/game_script/harness/list_stubs/GameHostCapsDouble.cpp), so the two cannot
// drift; GameHostCapsTest pins the real function to them and ModePickerTest the double.
namespace HostCapsValues {
// The most seats in one match (api-level-1.txt's seats_max).
inline constexpr int32_t MAX_SEATS = 2;
// The answer of a board that offers Play Nearby over EspNowLink (epic-play-nearby entry 11): the X4 Pro, the only
// board that offers it until a Sticky device run (owner's Decision of 2026-10-13, entry 20; GameHostCaps.cpp gates it
// per board). The harness double reports it as its default, standing in for the X4 Pro. The simulator reports nearby on
// regardless, over UdpLink (entry 16).
inline constexpr bool NEARBY_BUILT = true;
// Pass and Play: an open pass match with the fewest seats it can have (epic-pass-and-play).
inline constexpr bool PASS = true;
}  // namespace HostCapsValues

// The one HostCaps provider (spine AD-15): the API levels from ApiLevel.h and
// HostCapsValues: two seats, Pass and Play on, and Play Nearby on for the X4 Pro device build (over EspNowLink) and
// the simulator (over UdpLink, AD-2), off on every other games board.
GameCore::HostCaps gameHostCaps();

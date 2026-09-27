#pragma once

#include <HostCaps.h>

// The one HostCaps provider (spine AD-15): the API levels from ApiLevel.h, two
// seats, and Play Nearby off until epic-play-nearby turns it on for device builds.
// The simulator has no radio, so nearby stays false there (AD-2).
GameCore::HostCaps gameHostCaps();

#include <gtest/gtest.h>

#include <string>

#include "ApiLevel.h"
#include "ApiLevelList.h"
#include "GameHostCaps.h"

// The one HostCaps provider (src/games/GameHostCaps.cpp, built here with the games
// flag) against ApiLevel.h and the API list's seats_max, which it must report.

namespace {

TEST(GameHostCapsTest, ReportsTheApiLevels) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.api, API_LEVEL);
  EXPECT_EQ(caps.minApi, API_MIN_LEVEL);
}

TEST(GameHostCapsTest, MaxSeatsIsTheListedSeatsMax) {
  const ApiLevelList::Surface surface = ApiLevelList::loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::string seatsMax;
  for (const ApiLevelList::Entry& entry : surface.entries()) {
    if (entry.kind == "seats_max") seatsMax = entry.body;
  }
  ASSERT_FALSE(seatsMax.empty()) << "no seats_max entry";
  EXPECT_EQ(std::to_string(gameHostCaps().maxSeats), seatsMax);
}

}  // namespace

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ApiLevel.h"
#include "ApiLevelList.h"
#include "GameHostCaps.h"
#include "Manifest.h"

// The one HostCaps provider (src/games/GameHostCaps.cpp, built here with the games
// flag) against ApiLevel.h and the API list's seats_max, which it must report.

namespace {

TEST(GameHostCapsTest, ReportsTheApiLevels) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.api, API_LEVEL);
  EXPECT_EQ(caps.minApi, API_MIN_LEVEL);
}

// epic-pass-and-play runs an open pass match, so the host offers pass, and Manifest::check then
// offers a game with solo and pass both modes.
TEST(GameHostCapsTest, PassIsOnSoASoloAndPassGameOffersBothModes) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_TRUE(caps.pass);
  GameCore::Manifest game;
  std::strcpy(game.id, "g");
  std::strcpy(game.name, "G");
  game.api = caps.api;
  game.seatsMin = 1;
  game.seatsMax = 2;
  game.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
  const GameCore::CheckResult result = game.check(caps);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.modes, GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS);
}

// The harness's double reads the same HostCapsValues (ModePickerTest pins its defaults to them), so
// pinning the real function to them here keeps the two equal.
TEST(GameHostCapsTest, ReportsHostCapsValues) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.maxSeats, HostCapsValues::MAX_SEATS);
  EXPECT_EQ(caps.pass, HostCapsValues::PASS);
  EXPECT_EQ(caps.nearby, HostCapsValues::NEARBY_BUILT) << "this suite is not the simulator, so nearby is as built";
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

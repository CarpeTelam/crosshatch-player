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

// Until epic-pass-and-play, no match can run a pass game: the host says so, and Manifest::check
// then offers a game with solo and pass only its solo mode.
TEST(GameHostCapsTest, PassIsOffUntilPassAndPlay) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_FALSE(caps.pass);
  GameCore::Manifest game;
  std::strcpy(game.id, "g");
  std::strcpy(game.name, "G");
  game.api = caps.api;
  game.seatsMin = 1;
  game.seatsMax = 2;
  game.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
  const GameCore::CheckResult result = game.check(caps);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.modes, GameCore::Manifest::MODE_SOLO);
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

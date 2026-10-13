#include <gtest/gtest.h>

#include <cstring>

#include "ApiLevel.h"
#include "GameHostCaps.h"
#include "Manifest.h"

// GameHostCaps.cpp built with one board's defines (game_host_caps_boards.cmake builds this file once per board into its
// own executable, with BOARD_SUITE naming the suite and BOARD_NEARBY the answer that board must give). The owner's
// Decision of 2026-10-13 (epic-play-nearby, entry 20): Play Nearby is offered on the X4 Pro only until a Sticky device
// run, and the simulator reports it on whichever board it simulates (entry 16, R16). The rest of HostCaps is the same
// on every board.

#if !defined(BOARD_SUITE) || !defined(BOARD_NEARBY) || !defined(BOARD_IS_THE_X4PRO_DEVICE)
#error "game_host_caps_boards.cmake defines BOARD_SUITE, BOARD_NEARBY, and BOARD_IS_THE_X4PRO_DEVICE"
#endif

namespace {

constexpr bool EXPECT_NEARBY = BOARD_NEARBY != 0;

// A two-seat game declaring all three modes, as Manifest::check sees it on this board.
GameCore::CheckResult checkTheThreeModeGame(const GameCore::HostCaps& caps) {
  GameCore::Manifest game;
  std::strcpy(game.id, "g");
  std::strcpy(game.name, "G");
  game.api = caps.api;
  game.seatsMin = 1;
  game.seatsMax = 2;
  game.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS | GameCore::Manifest::MODE_NEARBY;
  return game.check(caps);
}

TEST(BOARD_SUITE, NearbyIsAsThisBoardOffersIt) { EXPECT_EQ(gameHostCaps().nearby, EXPECT_NEARBY); }

// A board that offers nearby offers a game declaring it all three modes; a board that does not leaves nearby out and
// the game stays valid with solo and pass, as before entry 11.
TEST(BOARD_SUITE, ATwoSeatGameThatDeclaresNearbyIsOfferedTheModesThisBoardCanStart) {
  const GameCore::CheckResult result = checkTheThreeModeGame(gameHostCaps());
  EXPECT_TRUE(result.ok());
  const uint8_t solo = GameCore::Manifest::MODE_SOLO;
  const uint8_t pass = GameCore::Manifest::MODE_PASS;
  const uint8_t nearby = GameCore::Manifest::MODE_NEARBY;
  EXPECT_EQ(result.modes, EXPECT_NEARBY ? (solo | pass | nearby) : (solo | pass));
}

TEST(BOARD_SUITE, TheOtherFieldsAreTheSameOnEveryBoard) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.api, API_LEVEL);
  EXPECT_EQ(caps.minApi, API_MIN_LEVEL);
  EXPECT_EQ(caps.maxSeats, HostCapsValues::MAX_SEATS);
  EXPECT_EQ(caps.pass, HostCapsValues::PASS);
}

#if BOARD_IS_THE_X4PRO_DEVICE
// The harness double (list_stubs/GameHostCapsDouble.cpp) stands in for this board and defaults to NEARBY_BUILT
// (ModePickerTest pins the double to it), so the X4 Pro build must answer that same value.
TEST(BOARD_SUITE, TheX4ProAnswersWhatTheHarnessDoubleDefaultsTo) {
  EXPECT_EQ(gameHostCaps().nearby, HostCapsValues::NEARBY_BUILT);
  EXPECT_TRUE(HostCapsValues::NEARBY_BUILT);
}
#endif

}  // namespace

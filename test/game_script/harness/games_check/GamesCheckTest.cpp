// The games check itself: every game in the games root (games/ by default), by id. Each is packed with the real packer
// (pack_games.py, at build time), installed with the real installer, and played from its companion folder
// (first_party/<id>/): its checks.lua and every round of rounds/*.lua. The ids are the directories of the two roots at
// configure time, so a new game adds tests here with no edit; with games/ missing or empty this executable has no
// per-id test and passes.
//
// The check's own tests are GamesCheckEngineTest's, over scratch trees in the build folder (never games/).

#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "GameCheck.h"
#include "HalDisplay.h"

// The converter reads the panel the installer's stubs describe.
HalDisplay display;

namespace {

using games_check::Report;
using games_check::Roots;

std::vector<std::string> idsFrom(const char* csv) {
  std::vector<std::string> ids;
  std::stringstream stream(csv);
  for (std::string id; std::getline(stream, id, ',');) {
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

Roots roots() { return Roots{GAMES_CHECK_GAMES_ROOT, GAMES_CHECK_COMPANION_ROOT, PACKED_GAMES_DIR}; }

std::string testName(const ::testing::TestParamInfo<std::string>& info) {
  return games_check::testNameOf(info.param, info.index);
}

// Notes (a skipped mode, a count) go to the log, never to a failure.
void expectGreen(const Report& report) {
  for (const std::string& note : report.notes) std::cerr << "[games-check] " << note << "\n";
  EXPECT_TRUE(report.ok()) << report.text();
}

class GamesCheckTest : public ::testing::TestWithParam<std::string> {};

class CompanionFolderTest : public ::testing::TestWithParam<std::string> {};

}  // namespace

TEST_P(GamesCheckTest, ThePackageInstallsAndLoads) { expectGreen(games_check::checkPackage(roots(), GetParam())); }

TEST_P(GamesCheckTest, TheGamesOwnChecksPass) { expectGreen(games_check::runGameChecks(roots(), GetParam())); }

TEST_P(GamesCheckTest, EveryRoundPlaysAsItsFileSays) { expectGreen(games_check::playRounds(roots(), GetParam())); }

TEST_P(CompanionFolderTest, HasAGame) { expectGreen(games_check::companionHasGame(roots(), GetParam())); }

INSTANTIATE_TEST_SUITE_P(Games, GamesCheckTest, ::testing::ValuesIn(idsFrom(GAMES_CHECK_GAME_IDS)), testName);
INSTANTIATE_TEST_SUITE_P(Companions, CompanionFolderTest, ::testing::ValuesIn(idsFrom(GAMES_CHECK_COMPANION_IDS)),
                         testName);
// With games/ missing or empty (and no companion folder) there is nothing to instantiate, which is no error.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GamesCheckTest);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CompanionFolderTest);

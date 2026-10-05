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

using games_check::CanvasSize;
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

Roots roots(const CanvasSize canvas) {
  return Roots{GAMES_CHECK_GAMES_ROOT, GAMES_CHECK_COMPANION_ROOT, PACKED_GAMES_DIR, canvas};
}

std::string testName(const ::testing::TestParamInfo<std::string>& info) {
  return games_check::testNameOf(info.param, info.index);
}

// Notes (a skipped mode, a count) go to the log, never to a failure.
void expectGreen(const Report& report) {
  for (const std::string& note : report.notes) std::cerr << "[games-check] " << note << "\n";
  EXPECT_TRUE(report.ok()) << report.text();
}

// Every game is played on both canvases, the Sticky's 474 x 788 (GamesCheckTest) and the X4 Pro's 466 x 788
// (GamesCheck466Test, the owner's Decision of 2026-10-05): the check names no game, so a game that breaks on either
// canvas fails a test of its own id here.
class GamesCheckTest : public ::testing::TestWithParam<std::string> {};

class GamesCheck466Test : public ::testing::TestWithParam<std::string> {};

class CompanionFolderTest : public ::testing::TestWithParam<std::string> {};

}  // namespace

TEST_P(GamesCheckTest, ThePackageInstallsAndLoads) {
  expectGreen(games_check::checkPackage(roots(games_check::CANVAS_474), GetParam()));
}

TEST_P(GamesCheckTest, TheGamesOwnChecksPass) {
  expectGreen(games_check::runGameChecks(roots(games_check::CANVAS_474), GetParam()));
}

TEST_P(GamesCheckTest, EveryRoundPlaysAsItsFileSays) {
  expectGreen(games_check::playRounds(roots(games_check::CANVAS_474), GetParam()));
}

TEST_P(GamesCheck466Test, ThePackageInstallsAndLoads) {
  expectGreen(games_check::checkPackage(roots(games_check::CANVAS_466), GetParam()));
}

TEST_P(GamesCheck466Test, TheGamesOwnChecksPass) {
  expectGreen(games_check::runGameChecks(roots(games_check::CANVAS_466), GetParam()));
}

TEST_P(GamesCheck466Test, EveryRoundPlaysAsItsFileSays) {
  expectGreen(games_check::playRounds(roots(games_check::CANVAS_466), GetParam()));
}

// Every round again with the restore probe (the Continue seam, row 6 of the cross-story review): before each step and
// after the last, the current snapshot is restored into a new game, which starts with every seat's `ui` empty and draws
// every local seat. The device builds its VM that way on Continue; no round played through it before.
TEST_P(GamesCheckTest, EveryRoundRestoresFromItsSnapshot) {
  games_check::PlayOptions options;
  options.restoreProbe = true;
  expectGreen(games_check::playRounds(roots(games_check::CANVAS_474), GetParam(), nullptr, options));
}

TEST_P(GamesCheck466Test, EveryRoundRestoresFromItsSnapshot) {
  games_check::PlayOptions options;
  options.restoreProbe = true;
  expectGreen(games_check::playRounds(roots(games_check::CANVAS_466), GetParam(), nullptr, options));
}

TEST_P(CompanionFolderTest, HasAGame) {
  expectGreen(games_check::companionHasGame(roots(games_check::CANVAS_474), GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Games, GamesCheckTest, ::testing::ValuesIn(idsFrom(GAMES_CHECK_GAME_IDS)), testName);
INSTANTIATE_TEST_SUITE_P(Games466, GamesCheck466Test, ::testing::ValuesIn(idsFrom(GAMES_CHECK_GAME_IDS)), testName);
INSTANTIATE_TEST_SUITE_P(Companions, CompanionFolderTest, ::testing::ValuesIn(idsFrom(GAMES_CHECK_COMPANION_IDS)),
                         testName);
// With games/ missing or empty (and no companion folder) there is nothing to instantiate, which is no error.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GamesCheckTest);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GamesCheck466Test);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CompanionFolderTest);

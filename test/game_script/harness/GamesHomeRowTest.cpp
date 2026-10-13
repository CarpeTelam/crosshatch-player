#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

#include "activities/games/GameLobbyActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GameOptionsActivity.h"
#include "activities/games/GamesHomeRow.h"
#include "activities/games/GamesLauncherActivity.h"

// GamesHomeRow.h (entry 10 of epic-play-nearby): the names ActivityManager::goHome maps to Home's Games row. The
// mapping is a pure function so the host tests it; the activities' own names are pinned to it, and the activities'
// sources are scanned for a name the list does not hold, which is how deferred-work.md ## 5.12 (GameOptions) and
// ## 4.13 were missed: a new screen under the Games row is a name to add here and a test failing until it is.

namespace {

std::string slurp(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool endsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

TEST(GamesHomeRowTest, TheFiveNamesAreUnderTheGamesRow) {
  EXPECT_TRUE(GamesHomeRow::isGamesActivity("GamesLauncher"));
  EXPECT_TRUE(GamesHomeRow::isGamesActivity("GameMode"));
  EXPECT_TRUE(GamesHomeRow::isGamesActivity("GameOptions"));
  EXPECT_TRUE(GamesHomeRow::isGamesActivity("GameLobby"));
  EXPECT_TRUE(GamesHomeRow::isGamesActivity("GameMatch"));
  EXPECT_STREQ(GamesHomeRow::GAMES_LAUNCHER, "GamesLauncher");
  EXPECT_STREQ(GamesHomeRow::GAME_MODE, "GameMode");
  EXPECT_STREQ(GamesHomeRow::GAME_OPTIONS, "GameOptions");
  EXPECT_STREQ(GamesHomeRow::GAME_LOBBY, "GameLobby");
  EXPECT_STREQ(GamesHomeRow::GAME_MATCH, "GameMatch");
}

TEST(GamesHomeRowTest, ANameOutsideTheListIsNotUnderTheGamesRow) {
  // Upstream's names (ActivityManager::goHome's other arms), near misses, and nothing.
  for (const char* name :
       {"", "Home", "FileBrowser", "Library", "OpdsBookBrowser", "PluginCatalog", "CrossPointWebServer", "Settings",
        "Reader", "Games", "Game", "GameMatches", "GameLobbyX", "gamemode", "GameMode ", " GameMatch"}) {
    EXPECT_FALSE(GamesHomeRow::isGamesActivity(name)) << '"' << name << '"';
  }
}

TEST(GamesHomeRowTest, EveryGamesActivityNamesItselfWithItsConstant) {
  EXPECT_STREQ(GamesLauncherActivity::NAME, GamesHomeRow::GAMES_LAUNCHER);
  EXPECT_STREQ(GameModeActivity::NAME, GamesHomeRow::GAME_MODE);
  EXPECT_STREQ(GameOptionsActivity::NAME, GamesHomeRow::GAME_OPTIONS);
  EXPECT_STREQ(GameLobbyActivity::NAME, GamesHomeRow::GAME_LOBBY);
  for (const char* name :
       {GamesLauncherActivity::NAME, GameModeActivity::NAME, GameOptionsActivity::NAME, GameLobbyActivity::NAME}) {
    EXPECT_TRUE(GamesHomeRow::isGamesActivity(name)) << name;
  }
}

// The names the activities pass to Activity's constructor, found in the sources (the match's is a literal in its
// constructor; the others are NAME constants): every one must be under the row.
TEST(GamesHomeRowTest, EveryNameInTheGamesActivitySourcesIsUnderTheGamesRow) {
  const std::filesystem::path dir = GAMES_ACTIVITIES_DIR;
  ASSERT_TRUE(std::filesystem::is_directory(dir)) << dir;
  const std::regex constant(R"re(NAME\s*=\s*"([A-Za-z0-9_]+)")re");
  const std::regex constructed(R"re((?:Activity|UiListActivity|UiTabListActivity)\(\s*"([A-Za-z0-9_]+)")re");
  std::set<std::string> found;
  size_t scanned = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const std::string file = entry.path().filename().string();
    if (!endsWith(file, "Activity.h") && !endsWith(file, "Activity.cpp")) continue;
    ++scanned;
    const std::string text = slurp(entry.path());
    for (const std::regex& pattern : {constant, constructed}) {
      for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) found.insert((*it)[1]);
    }
  }
  EXPECT_GE(scanned, 10u) << "the scan found the activities' headers and sources";
  ASSERT_TRUE(found.count("GameMatch")) << "the match's literal name is read from GameMatchActivity.cpp";
  for (const std::string& name : found) {
    EXPECT_TRUE(GamesHomeRow::isGamesActivity(name))
        << name << " is an activity under src/activities/games that goHome would not map to the Games row";
  }
  // The two that read their name from the header have no literal in their sources.
  EXPECT_EQ(found.count("GameMode"), 0u) << "GameModeActivity::NAME is GamesHomeRow::GAME_MODE, not a second literal";
  EXPECT_EQ(found.count("GameLobby"), 0u)
      << "GameLobbyActivity::NAME is GamesHomeRow::GAME_LOBBY, not a second literal";
}

// goHome (ActivityManager.cpp) is not built on the host: its one call and the absence of a name list are read from the
// source, so the guards that stay (an explicit item wins; the games arm last, inside the build guard) are not lost to a
// later edit.
TEST(GamesHomeRowTest, GoHomeMapsTheGamesRowThroughTheHeaderAndOnlyInsideTheGamesBuild) {
  const std::string text = slurp(std::filesystem::path(GAMES_ACTIVITIES_DIR) / ".." / "ActivityManager.cpp");
  ASSERT_FALSE(text.empty());
  // The whole statement, so a flipped test or another Home item fails here: goHome is device code, not built on the
  // host.
  const std::string statement =
      "if (GamesHomeRow::isGamesActivity(activityName)) initialMenuItem = HomeMenuItem::GAMES;";
  const size_t call = text.find(statement);
  ASSERT_NE(call, std::string::npos) << "goHome must read: " << statement;
  const size_t open = text.rfind("#if FREEINK_CAP_GAMES", call);
  const size_t close = text.find("#endif", call);
  ASSERT_NE(open, std::string::npos);
  ASSERT_NE(close, std::string::npos);
  EXPECT_EQ(text.find("#endif", open), close) << "the call is the guarded block's own";
  EXPECT_NE(text.find("initialMenuItem == HomeMenuItem::NONE && currentActivity"), std::string::npos)
      << "an explicit item still wins";
  EXPECT_LT(text.find("activityName == \"Settings\""), call) << "the games arm comes after upstream's";
  EXPECT_EQ(text.find("activityName == GameModeActivity::NAME"), std::string::npos);
  EXPECT_EQ(text.find("\"GameMatch\""), std::string::npos) << "no name list left in goHome";
  EXPECT_EQ(text.find("GameModeActivity.h"), std::string::npos);
}

}  // namespace

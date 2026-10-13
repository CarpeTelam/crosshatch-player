#pragma once

#include <string_view>

// Which activities sit under Home's Games row: the names ActivityManager::goHome maps to HomeMenuItem::GAMES (ledger
// row 5), so a Home from any screen of the Games section lands on the Games row. One header, pure (std only), so the
// mapping is testable on the host and a new screen's name is added in one place (GamesHomeRowTest scans
// src/activities/games for names missed here; deferred-work.md ## 5.12 was exactly one).
//
// Each constant is the name its activity passes to Activity's constructor. GameModeActivity::NAME and
// GameLobbyActivity::NAME read theirs from here; the other three activities' headers are not this entry's, so
// GamesHomeRowTest pins GamesLauncherActivity::NAME and GameOptionsActivity::NAME to these, and the match's literal
// name by reading its source.
namespace GamesHomeRow {

inline constexpr const char* GAMES_LAUNCHER = "GamesLauncher";
inline constexpr const char* GAME_MODE = "GameMode";
inline constexpr const char* GAME_OPTIONS = "GameOptions";
inline constexpr const char* GAME_LOBBY = "GameLobby";
inline constexpr const char* GAME_MATCH = "GameMatch";

// True for the name of any activity under the Games row.
inline constexpr bool isGamesActivity(const std::string_view name) {
  return name == GAMES_LAUNCHER || name == GAME_MODE || name == GAME_OPTIONS || name == GAME_LOBBY ||
         name == GAME_MATCH;
}

}  // namespace GamesHomeRow

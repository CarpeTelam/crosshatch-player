# Entry 10 of epic-play-nearby: GameLobbyActivity.cpp is a list screen over the screen doubles, like GameModeActivity.cpp,
# so it stays out of the shared globbed libraries (which have no UiListActivity or theme) and of game_match_src.
# nearby_lobby.cmake adds it to game_launcher_src, which the title screen (GameModeActivity.cpp, whose New game pushes the
# lobby) and every suite that links it need.
list(APPEND HARNESS_EXCLUDE_ACTIVITIES GameLobbyActivity.cpp)

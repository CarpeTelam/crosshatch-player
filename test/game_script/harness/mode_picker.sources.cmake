# Entry 9 of epic-install-and-launcher: GameModeActivity.cpp needs the screen doubles, so, like the launcher, it stays
# out of the shared globbed libraries (which have no UiListActivity or theme) and is built by games_launcher.cmake
# into game_launcher_src, which mode_picker.cmake's suite links.
list(APPEND HARNESS_EXCLUDE_ACTIVITIES GameModeActivity.cpp)
# Entry 12 of epic-pass-and-play: the title screen's Options (GameOptionsActivity.cpp), a list screen on the same doubles,
# is built with it in game_launcher_src. GamePicture.cpp, which the title screen draws its splash with, needs only the
# renderer, storage, and PSRAM doubles, so it stays in the shared libraries and in match.cmake's game_match_src.
list(APPEND HARNESS_EXCLUDE_ACTIVITIES GameOptionsActivity.cpp)
# Entry 12's follow-up: the splash layout the title screen and the hidden hand-off share (GameSplashLayout.cpp) reads the
# theme and the FreeInkUI screen, so, like the match, it is built against the screen doubles in match.cmake's
# game_match_src (which game_launcher_src links), never in the shared libraries.
list(APPEND HARNESS_EXCLUDE_ACTIVITIES GameSplashLayout.cpp)

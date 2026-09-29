# Entry 9 of epic-install-and-launcher: GameModeActivity.cpp needs the screen doubles, so, like the launcher, it stays
# out of the shared globbed libraries (which have no UiListActivity or theme) and is built by games_launcher.cmake
# into game_launcher_src, which mode_picker.cmake's suite links.
list(APPEND HARNESS_EXCLUDE_ACTIVITIES GameModeActivity.cpp)

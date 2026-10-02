# The Games launcher on the host: GamesLauncherActivity, built unchanged over the screen doubles and
# the match's source library (match.cmake's game_match_src: the real Activity, UiListActivity,
# ButtonNavigator, FreeInkUI, GameRegistry, GameRowIcon, and the match a row opens). Entry 5 of
# epic-install-and-launcher built the suite for the minimal Games list; entry 8 renamed it and
# tests the launcher on it (GamesLauncherTest.cpp, and GameRowIconTest.cpp for the row icons' pixels).
#
# The installer is scripted (list_stubs/): the launcher calls hasInbox and installAll and shows what
# they report, and the installer over real packages has its own suites. Everything else is real.

add_library(game_launcher_src STATIC
  ${REPO_ROOT}/src/activities/games/GamesLauncherActivity.cpp
  # The launcher opens the title screen, which mode_picker.cmake tests.
  ${REPO_ROOT}/src/activities/games/GameModeActivity.cpp
  # The title screen pushes its Options (entry 12 of epic-pass-and-play).
  ${REPO_ROOT}/src/activities/games/GameOptionsActivity.cpp
  ${HARNESS_DIR}/list_stubs/GamePackageInstallerDouble.cpp
  # The launcher's Remove calls GamePackageInstaller::remove; remove_game.cmake tests the launcher over this script.
  ${HARNESS_DIR}/list_stubs/GamePackageInstallerRemoveDouble.cpp)
target_include_directories(game_launcher_src PUBLIC ${HARNESS_DIR}/list_stubs)
# What GameRegistry (in game_match_src) reads a manifest with. Its own library, listed after game_match_src,
# so that it is linked whether or not the list screen itself refers to a symbol in it.
add_library(game_launcher_manifest STATIC
  ${REPO_ROOT}/lib/GameCore/Manifest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp)
target_link_libraries(game_launcher_manifest PUBLIC game_harness_doubles)
target_link_libraries(game_launcher_src PUBLIC game_match_src game_launcher_manifest)

# GameHostCapsDouble.cpp is in the executable, not a library, so it wins over GameHostCaps.cpp in game_match_src.
add_executable(GamesLauncherHarnessTest GamesLauncherTest.cpp GameRowIconTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(GamesLauncherHarnessTest PRIVATE
  # The fixture games (tracer, timer, counter) a row opens.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
# GamesLauncherTest lists every installer Error in a switch with no default: a new one must not compile until it is added.
# Copies of this suite (entries 9 to 12) must keep this option.
target_compile_options(GamesLauncherHarnessTest PRIVATE -Werror=switch)
target_link_libraries(GamesLauncherHarnessTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(GamesLauncherHarnessTest)

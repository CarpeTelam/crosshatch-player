# A game's title screen on the host: GameModeActivity (the mode picker of entry 9 of epic-install-and-launcher, the title
# screen since entry 7 of epic-pass-and-play) and the launcher that opens it, one row a game since entry 8, whose
# saves-on-the-card cases also remove a game (list_stubs/RemoveScript.h), over the screen doubles. The
# libraries are games_launcher.cmake's game_launcher_src (the launcher, the title screen, the scripted installer, and the
# match's source library); this suite is its own executable on the same pattern as GamesLauncherHarnessTest, with the
# same scripted host caps (list_stubs/HostCapsScript.h).

add_executable(ModePickerHarnessTest ModePickerTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(ModePickerHarnessTest PRIVATE
  # The fixture games (counter, pass-open) a row starts.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
# Copies of GamesLauncherTest keep -Werror=switch (games_launcher.cmake).
target_compile_options(ModePickerHarnessTest PRIVATE -Werror=switch)
target_link_libraries(ModePickerHarnessTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(ModePickerHarnessTest)

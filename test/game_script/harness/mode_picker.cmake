# The mode picker on the host: GameModeActivity and the launcher that opens it, unchanged, over the screen doubles
# (entry 9 of epic-install-and-launcher). The libraries are games_launcher.cmake's game_launcher_src (the launcher, the
# picker, the scripted installer, and the match's source library); this suite is its own executable on the same pattern
# as GamesLauncherHarnessTest, with the same scripted host caps (list_stubs/HostCapsScript.h).

add_executable(ModePickerHarnessTest ModePickerTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(ModePickerHarnessTest PRIVATE
  # The fixture games (counter) a pick starts.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
# Copies of GamesLauncherTest keep -Werror=switch (games_launcher.cmake).
target_compile_options(ModePickerHarnessTest PRIVATE -Werror=switch)
target_link_libraries(ModePickerHarnessTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(ModePickerHarnessTest)

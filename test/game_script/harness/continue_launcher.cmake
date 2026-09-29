# Continue in the launcher (ticket 12 of epic-install-and-launcher): the Continue rows GamesLauncherActivity lists above the
# games for each valid resume.bin, over the real GameSaveStore::peek and GameMatchActivity, the fake card, and the
# launcher's scripted installer and host caps (games_launcher.cmake's game_launcher_src, as remove_game.cmake).
add_executable(ContinueLauncherTest ContinueLauncherTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(ContinueLauncherTest PRIVATE
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
# Copies of GamesLauncherTest keep -Werror=switch (games_launcher.cmake).
target_compile_options(ContinueLauncherTest PRIVATE -Werror=switch)
target_link_libraries(ContinueLauncherTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(ContinueLauncherTest)

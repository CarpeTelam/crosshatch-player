# Removing a game (ticket 10 of epic-install-and-launcher): two executables.
#  - GameRemoveTest: the real GamePackageInstaller::remove over the fake card (installer.cmake's game_installer_src),
#    with the real install beside it for the reinstall.
#  - GameRemoveLauncherTest: the launcher's confirmation, whole-page paging, and return to the page of the game
#    last opened, over a scripted remove (list_stubs/RemoveScript.h, linked into game_launcher_src by
#    games_launcher.cmake) and the launcher's scripted host caps.

add_executable(GameRemoveTest GameRemoveTest.cpp)
# InstallerSupport.h names the golden vector's folder; this suite installs packages it builds itself.
target_compile_definitions(GameRemoveTest PRIVATE PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(GameRemoveTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(GameRemoveTest)

add_executable(GameRemoveLauncherTest GameRemoveLauncherTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(GameRemoveLauncherTest PRIVATE
  # The fixture games (counter) a row opens.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
# Copies of GamesLauncherTest keep -Werror=switch (games_launcher.cmake).
target_compile_options(GameRemoveLauncherTest PRIVATE -Werror=switch)
target_link_libraries(GameRemoveLauncherTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(GameRemoveLauncherTest)

# The installer's count of the packages that wait for room (Report::waiting; AI-2 of the epic-install-and-launcher
# retrospective), over the fake card at the game limit, and the launcher's install after a remove (remove, then
# installAll) on the real installer. It runs after installer.cmake by name and links entry 3's
# game_installer_src unchanged; GameInstallerTest, which also covers the limit, is left to its own lane.

add_executable(InstallerWaitingTest InstallerWaitingTest.cpp)
target_compile_definitions(InstallerWaitingTest PRIVATE PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(InstallerWaitingTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(InstallerWaitingTest)

# The installer's manifest icon check and the pass capability (entry 7 of epic-install-and-launcher): an
# icon the library lacks ends the package .bad, a malformed icon_weight does too, and a pass-only game
# installs but the registry marks it unavailable. It runs after installer.cmake by name and links entry 3's
# game_installer_src unchanged.

add_executable(InstallerIconTest InstallerIconTest.cpp)
target_compile_definitions(InstallerIconTest PRIVATE PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(InstallerIconTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(InstallerIconTest)

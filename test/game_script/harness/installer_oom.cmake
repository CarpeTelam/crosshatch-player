# The installer with each nothrow allocation of an install failed in turn (e4-r1): its own executable, because
# GameInstallerOomTest.cpp replaces the nothrow operator new and new[], and that must reach no other suite. It links
# installer.cmake's game_installer_src, as remove_game.cmake does.

add_executable(GameInstallerOomTest GameInstallerOomTest.cpp)
# InstallerSupport.h names the golden vector's folder; this suite installs packages it builds itself.
target_compile_definitions(GameInstallerOomTest PRIVATE PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(GameInstallerOomTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(GameInstallerOomTest)

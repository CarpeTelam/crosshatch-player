# Home on the host: HomeActivity.cpp and CoverGridHomeUi.cpp, built unchanged over the screen doubles.
# Entry 5 of epic-install-and-launcher, for deferred-work `## 3.6`: the cover grid's tab order against
# HomeActivity's index mapping.
#
# Neither file builds in place (the record is `## 4.4` in deferred-work.md and the plan of entry 5), so, as
# match.cmake does for Activity, the suite builds copies of them made at configure time, byte for byte, in
# a folder of its own (a change to the real files is what the suite builds):
#   - CoverGridHomeUi.h includes "UiAppHost.h" and "UITheme.h" by a sibling path, which in the
#     firmware's folder finds the real ones; in the copy's folder they fall through to home_stubs/,
#     which forwards to the screen doubles.
#   - HomeActivity.h includes "components/CoverGridHomeUi.h", which the copies' folder answers first.
# The headers the host lacks (BoardConfig.h, FsHelpers.h, RecentBooksStore.h, Epub.h, Xtc.h,
# CrossPointSettings.h, CrossPointState.h, OpdsServerStore.h, Bitmap.h, HomeCoverCache.h) are doubles in
# home_stubs/, each with the surface Home calls and nothing else. LibraryIndexFile.h and LibraryBuilder.h are the
# real headers, their few members Home calls defined in HomeStubs.cpp (the real LibraryIndexFile.cpp reads the card
# with HalFile::fileSize64, which the shared fake card does not have).

set(HOME_STUBS_DIR ${HARNESS_DIR}/home_stubs)
set(REAL_HOME_DIR ${CMAKE_CURRENT_BINARY_DIR}/real_home)
foreach(name HomeActivity.h HomeActivity.cpp)
  configure_file(${REPO_ROOT}/src/activities/home/${name} ${REAL_HOME_DIR}/activities/home/${name} COPYONLY)
endforeach()
foreach(name CoverGridHomeUi.h CoverGridHomeUi.cpp)
  configure_file(${REPO_ROOT}/src/components/${name} ${REAL_HOME_DIR}/components/${name} COPYONLY)
endforeach()
foreach(name blocks book folder library settings2 transfer)
  configure_file(${REPO_ROOT}/src/components/icons/${name}.h ${REAL_HOME_DIR}/icons/${name}.h COPYONLY)
endforeach()

add_library(game_home_src STATIC
  ${REAL_HOME_DIR}/activities/home/HomeActivity.cpp
  ${REAL_HOME_DIR}/components/CoverGridHomeUi.cpp
  ${HOME_STUBS_DIR}/HomeStubs.cpp)
target_include_directories(game_home_src BEFORE PUBLIC ${REAL_HOME_DIR} ${HOME_STUBS_DIR})
target_include_directories(game_home_src PUBLIC ${REPO_ROOT}/lib/LibraryIndex)
target_link_libraries(game_home_src PUBLIC game_match_src)

add_executable(HomeHarnessTest HomeTabsTest.cpp)
target_compile_definitions(HomeHarnessTest PRIVATE
  # MatchSupport.h, the screen fixture, reads the fixture games from here.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(HomeHarnessTest PRIVATE game_home_src GTest::gtest_main)
gtest_discover_tests(HomeHarnessTest)

# The same two files and the same tests as the boards without games ship them (default, x4c, papermono): no
# Games row, no Games tab, five icons. game_match_src's PUBLIC FREEINK_CAP_GAMES=1 reaches everything that
# links it, and a compile option comes after the definitions on the command line, so -U wins for these
# objects and the test built with them. The exe is its own, since both variants define the same classes.
add_library(game_home_off_src STATIC
  ${REAL_HOME_DIR}/activities/home/HomeActivity.cpp
  ${REAL_HOME_DIR}/components/CoverGridHomeUi.cpp
  ${HOME_STUBS_DIR}/HomeStubs.cpp)
target_compile_options(game_home_off_src PRIVATE -UFREEINK_CAP_GAMES)
target_include_directories(game_home_off_src BEFORE PUBLIC ${REAL_HOME_DIR} ${HOME_STUBS_DIR})
target_include_directories(game_home_off_src PUBLIC ${REPO_ROOT}/lib/LibraryIndex)
target_link_libraries(game_home_off_src PUBLIC game_match_src)

add_executable(HomeGamesOffHarnessTest HomeTabsTest.cpp)
target_compile_definitions(HomeGamesOffHarnessTest PRIVATE
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_compile_options(HomeGamesOffHarnessTest PRIVATE -UFREEINK_CAP_GAMES)
target_link_libraries(HomeGamesOffHarnessTest PRIVATE game_home_off_src GTest::gtest_main)
gtest_discover_tests(HomeGamesOffHarnessTest TEST_PREFIX GamesOff.)

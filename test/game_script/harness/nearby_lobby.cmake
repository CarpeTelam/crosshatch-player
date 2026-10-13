# The Nearby lobby and the title screen's Nearby start on the host (entry 10 of epic-play-nearby): GameLobbyActivity, the
# real one, over the screen doubles and a FakeLink radio (NearbyLobbySupport.h's FakeNearbyRadio), the host and the
# guest lobbies starting the nearby fixture's match with the session moved in (NearbyLobbyTest.cpp); and the pure
# header that maps activity names to Home's Games row (GamesHomeRowTest.cpp).
#
# GameLobbyActivity.cpp and the scripted NearbyProbe::read() join game_launcher_src (games_launcher.cmake: the title screen
# calls the lobby and the probe, so every suite that links it needs both). The real NearbyProbe.cpp is device only and
# stays excluded by name (esp_now_link.sources.cmake).
target_sources(game_launcher_src PRIVATE
  ${REPO_ROOT}/src/activities/games/GameLobbyActivity.cpp
  ${HARNESS_DIR}/list_stubs/NearbyProbeDouble.cpp)

# GameHostCapsDouble.cpp is in the executable, not a library, so it wins over GameHostCaps.cpp in game_match_src.
add_executable(NearbyLobbyHarnessTest NearbyLobbyTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(NearbyLobbyHarnessTest PRIVATE
  # The nearby fixture game each device plays.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_compile_options(NearbyLobbyHarnessTest PRIVATE -Werror=switch)
target_link_libraries(NearbyLobbyHarnessTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(NearbyLobbyHarnessTest)

add_executable(GamesHomeRowHarnessTest GamesHomeRowTest.cpp
  ${HARNESS_DIR}/list_stubs/GameHostCapsDouble.cpp)
target_compile_definitions(GamesHomeRowHarnessTest PRIVATE
  # The activities' sources, scanned for names GamesHomeRow.h does not list.
  GAMES_ACTIVITIES_DIR="${REPO_ROOT}/src/activities/games")
target_link_libraries(GamesHomeRowHarnessTest PRIVATE game_launcher_src GTest::gtest_main)
gtest_discover_tests(GamesHomeRowHarnessTest)

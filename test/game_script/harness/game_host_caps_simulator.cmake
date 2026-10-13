# gameHostCaps() as the simulator builds it (entry 16 of epic-play-nearby): GameHostCaps.cpp with SIMULATOR on reports
# nearby on, while HostCapsValues::NEARBY_BUILT stays false. The shared libraries hold a copy built without SIMULATOR,
# so this suite builds its own copy into its own executable and links neither game_match_src nor game_harness_src (a
# second gameHostCaps() would be a duplicate definition).

add_library(game_host_caps_simulator STATIC ${REPO_ROOT}/src/games/GameHostCaps.cpp)
target_compile_definitions(game_host_caps_simulator PRIVATE SIMULATOR=1 FREEINK_CAP_GAMES=1)
target_include_directories(game_host_caps_simulator PUBLIC ${REPO_ROOT}/lib/GameCore ${REPO_ROOT}/src/games)

add_executable(GameHostCapsSimulatorTest GameHostCapsSimulatorTest.cpp)
target_link_libraries(GameHostCapsSimulatorTest PRIVATE game_host_caps_simulator GTest::gtest_main)
gtest_discover_tests(GameHostCapsSimulatorTest)

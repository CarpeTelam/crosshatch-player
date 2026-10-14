# Two GameMatchActivity instances over FakeLink on real threads (entry 9 of epic-play-nearby): the nearby fixture played
# to over, the waiting screen, the rejected move, Play again, and nothing saved (NearbyMatchTest.cpp). The activities,
# GameVM, NearbySession, and GameLink are game_match_src's (match.cmake), over the screen doubles; the rig is
# NearbySessionSupport.h's Duo, the pair of NearbySessions on one FakeLinkNet. The first-party games are played in a
# nearby match too (cross-story review row 9): each from its game folder and its companion's round file, read with the
# games check's RoundFile (games_check_core, games_check.cmake); only the two folders are named here, never a game.

add_executable(NearbyMatchHarnessTest
  NearbyMatchTest.cpp)
target_compile_definitions(NearbyMatchHarnessTest PRIVATE
  # The nearby fixture game, installed under a game id for each device.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures"
  # The first-party games' folders and their companion folders (rounds/*.lua and the modules they require).
  NEARBY_GAMES_DIR="${REPO_ROOT}/games"
  NEARBY_COMPANION_DIR="${REPO_ROOT}/test/game_script/first_party")
target_link_libraries(NearbyMatchHarnessTest PRIVATE game_match_src games_check_core GTest::gtest_main)
gtest_discover_tests(NearbyMatchHarnessTest)

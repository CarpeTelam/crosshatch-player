# Two GameMatchActivity instances over FakeLink on real threads (entry 9 of epic-play-nearby): the nearby fixture played
# to over, the waiting screen, the rejected move, Play again, and nothing saved (NearbyMatchTest.cpp). The activities,
# GameVM, NearbySession, and GameLink are game_match_src's (match.cmake), over the screen doubles; the rig is
# NearbySessionSupport.h's Duo, the pair of NearbySessions on one FakeLinkNet.

add_executable(NearbyMatchHarnessTest
  NearbyMatchTest.cpp)
target_compile_definitions(NearbyMatchHarnessTest PRIVATE
  # The nearby fixture game, installed under a game id for each device.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(NearbyMatchHarnessTest PRIVATE game_match_src GTest::gtest_main)
gtest_discover_tests(NearbyMatchHarnessTest)

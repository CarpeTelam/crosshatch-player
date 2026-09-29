# GameAssets::load, FrameReplay, and drawGameIconAt over the harness's doubles: the
# suite's fakes, the folder loader (spans, offsets, both passes, the failure results),
# and the replay (icon, image, weight, fills). Entry 1 of epic-install-and-launcher.
add_executable(GameHarnessTest
  HarnessDoublesTest.cpp
  GameAssetsLoadTest.cpp
  FrameReplayTest.cpp)

target_link_libraries(GameHarnessTest PRIVATE
  game_harness_src
  game_harness_core
  GTest::gtest_main
)

gtest_discover_tests(GameHarnessTest)

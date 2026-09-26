add_executable(GameCoreTest
  GameCoreTest.cpp
  ${REPO_ROOT}/lib/GameCore/GameCore.cpp
)

target_include_directories(GameCoreTest PRIVATE
  ${REPO_ROOT}/lib/GameCore
)

target_link_libraries(GameCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GameCoreTest)

add_executable(GameScriptTest
  GameScriptTest.cpp
  ${REPO_ROOT}/lib/GameScript/GameScript.cpp
  ${REPO_ROOT}/lib/GameIcons/GameIcons.cpp
)

target_include_directories(GameScriptTest PRIVATE
  ${REPO_ROOT}/lib/GameScript
  ${REPO_ROOT}/lib/GameIcons
)

target_link_libraries(GameScriptTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GameScriptTest)

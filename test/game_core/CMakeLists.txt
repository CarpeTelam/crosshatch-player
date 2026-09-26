add_executable(GameCoreTest
  GameCoreTest.cpp
  ForkReleaseTest.cpp
  ${REPO_ROOT}/lib/GameCore/GameCore.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GameCoreTest PRIVATE
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/JsonParser
)

# Read at run time so the suite and the release workflow share one vector file.
target_compile_definitions(GameCoreTest PRIVATE
  FORK_VERSION_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fork_version_vectors.json"
)

target_link_libraries(GameCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GameCoreTest)

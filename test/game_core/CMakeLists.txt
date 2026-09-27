add_executable(GameCoreTest
  GameCoreTest.cpp
  ApiLevelTest.cpp
  ForkReleaseTest.cpp
  ManifestCheckTest.cpp
  ManifestTest.cpp
  SessionTest.cpp
  ${REPO_ROOT}/lib/GameCore/GameCore.cpp
  ${REPO_ROOT}/lib/GameCore/Manifest.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GameCoreTest PRIVATE
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
)

# Read at run time so the suite and the fork scripts share one vector file, API list, and header.
target_compile_definitions(GameCoreTest PRIVATE
  FORK_VERSION_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fork_version_vectors.json"
  API_LEVEL_LIST_DIR="${REPO_ROOT}/docs/crosshatch"
  API_LEVEL_HEADER_PATH="${REPO_ROOT}/lib/GameCore/ApiLevel.h"
)

target_link_libraries(GameCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GameCoreTest)

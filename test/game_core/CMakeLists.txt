add_executable(GameCoreTest
  ApiLevelTest.cpp
  ForkReleaseTest.cpp
  GameImagesTest.cpp
  GameHostCapsTest.cpp
  ManifestCheckTest.cpp
  ManifestTest.cpp
  MatchLifecycleTest.cpp
  ModeTableTest.cpp
  PackageLimitsTest.cpp
  PauseClockTest.cpp
  SeatShownTest.cpp
  SessionTest.cpp
  ${REPO_ROOT}/lib/GameCore/GameImages.cpp
  ${REPO_ROOT}/lib/GameCore/Manifest.cpp
  ${REPO_ROOT}/lib/GameCore/MatchLifecycle.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/src/games/GameHostCaps.cpp
)

# The HostCaps provider is games-only code; only its own source gets the flag.
set_source_files_properties(${REPO_ROOT}/src/games/GameHostCaps.cpp PROPERTIES COMPILE_DEFINITIONS FREEINK_CAP_GAMES=1)

target_include_directories(GameCoreTest PRIVATE
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
  # GameHostCaps.h, the HostCaps provider.
  ${REPO_ROOT}/src/games
)

# Read at run time so the suite and the fork scripts share one vector file, API list, and header.
target_compile_definitions(GameCoreTest PRIVATE
  # ManifestTest parses every fixture game's manifest.json (fixtures stay under test/, never games/).
  GAME_SCRIPT_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures"
  FORK_VERSION_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fork_version_vectors.json"
  PACKAGE_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/package_vectors.json"
  API_LEVEL_LIST_DIR="${REPO_ROOT}/docs/crosshatch"
  API_LEVEL_HEADER_PATH="${REPO_ROOT}/lib/GameCore/ApiLevel.h"
)

target_link_libraries(GameCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GameCoreTest)

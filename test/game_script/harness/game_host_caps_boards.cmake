# gameHostCaps() as each games board builds it (entry 20 of epic-play-nearby; owner's Decision of 2026-10-13: Play Nearby
# is offered on the X4 Pro only until a Sticky device run). GameHostCaps.cpp reads FREEINK_DEVICE_* and SIMULATOR, and
# the shared libraries hold one copy built for one board, so each board here is its own copy in its own executable
# (linking neither game_match_src nor game_harness_src: a second gameHostCaps() would be a duplicate definition), with
# the defines that board's environment sets (platformio.ini; the simulator's envs add SIMULATOR to the same device define,
# .claude/skills/run-crosshatch-player/simulator.ini). The no-board copy (no device define, as game_core's
# GameHostCapsTest builds it) stands for any other games board and is pinned off there.
#
# function arguments: SUITE, the gtest suite name; NEARBY, 1 or 0, what the board must answer; DEFINES, the board's
# defines; X4PRO_DEVICE, 1 for the one build the harness double stands in for.
function(add_game_host_caps_board_suite SUITE NEARBY X4PRO_DEVICE)
  set(LIB ${SUITE}_src)
  add_library(${LIB} STATIC ${REPO_ROOT}/src/games/GameHostCaps.cpp)
  target_compile_definitions(${LIB} PRIVATE FREEINK_CAP_GAMES=1 ${ARGN})
  target_include_directories(${LIB} PUBLIC ${REPO_ROOT}/lib/GameCore ${REPO_ROOT}/src/games)

  add_executable(${SUITE} GameHostCapsBoardsTest.cpp)
  target_compile_definitions(${SUITE} PRIVATE BOARD_SUITE=${SUITE} BOARD_NEARBY=${NEARBY}
                                              BOARD_IS_THE_X4PRO_DEVICE=${X4PRO_DEVICE})
  # Manifest::check, which the suite asks what a board offers a three-mode game.
  target_sources(${SUITE} PRIVATE ${REPO_ROOT}/lib/GameCore/Manifest.cpp
                                  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
  target_include_directories(${SUITE} PRIVATE ${REPO_ROOT}/lib/JsonParser ${REPO_ROOT}/lib/Memory
                                              ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
  target_link_libraries(${SUITE} PRIVATE ${LIB} GTest::gtest_main)
  gtest_discover_tests(${SUITE})
endfunction()

add_game_host_caps_board_suite(GameHostCapsX4ProTest 1 1 FREEINK_DEVICE_X4PRO=1)
add_game_host_caps_board_suite(GameHostCapsStickyTest 0 0 FREEINK_DEVICE_STICKY=1)
add_game_host_caps_board_suite(GameHostCapsNoBoardTest 0 0)
add_game_host_caps_board_suite(GameHostCapsSimulatorX4ProTest 1 0 SIMULATOR=1 FREEINK_DEVICE_X4PRO=1)
add_game_host_caps_board_suite(GameHostCapsSimulatorStickyTest 1 0 SIMULATOR=1 FREEINK_DEVICE_STICKY=1)

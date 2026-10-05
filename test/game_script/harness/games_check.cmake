# The games check (epic-first-party-games, entry 8): every game in the games root is packed with the real scripts/pack_game.py,
# installed with the real installer, and played headlessly from the companion folder's rounds (test/game_script/first_party/
# <id>/rounds/*.lua) and its checks.lua, over Session and MatchRounds. The target names no game: the ids are the directories of
# the two roots, found at configure time (CONFIGURE_DEPENDS re-globs them on every build), so a game added to games/ is
# checked with no edit here, and a root that does not exist (games/ before the first game) is an empty list, which passes.
#
# Cache variables, for a one-off run over scratch trees (both made absolute against the repository root):
#   GAMES_CHECK_GAMES_ROOT       default games/
#   GAMES_CHECK_COMPANION_ROOT   default test/game_script/first_party
#
# Targets:
#   packed_games          pack_games.py over every id of the games root (one custom command; its output is packed.stamp)
#   games_check_core      ScriptVm, RoundFile, RoundPlayer: the scripts' VM, the round file, the round's player
#   GamesCheckTest        the production check: eight tests per game id (the package, the game's own checks, every round,
#                         every round restored from its snapshot: each on the Sticky's 474 x 788 canvas and the X4 Pro's
#                         466 x 788), and one per companion id (HasAGame); see GamesCheckTest.cpp
#   GamesCheckEngineTest  the check's own tests: scratch trees under the build folder, never games/ or first_party/; it also
#                         holds BoardInsetsTest, which compiles the SDK's BoardConfig.h through board_stubs/ (that file alone)
#   GamesCheckFlowTest    pins the hidden flow of RoundPlayer to GameVM's (it links the match's screen doubles)
#   GamesCheckNoteImages  a plain ctest: Sudoku's make_note_images.py --check, when the tool exists
# All are labelled games-check: `ctest -L games-check`. Needs installer.cmake and match.cmake's libraries by name.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(GAMES_CHECK_REPO_ROOT "${REPO_ROOT}" ABSOLUTE)
set(GAMES_CHECK_GAMES_ROOT "${GAMES_CHECK_REPO_ROOT}/games" CACHE PATH "The folder of <id>/ game folders the games check packs and plays")
set(GAMES_CHECK_COMPANION_ROOT "${GAMES_CHECK_REPO_ROOT}/test/game_script/first_party" CACHE PATH
    "The folder of <id>/ companion folders (rounds/*.lua, checks.lua, modules) the games check plays")
get_filename_component(GAMES_CHECK_GAMES_ROOT_ABS "${GAMES_CHECK_GAMES_ROOT}" ABSOLUTE BASE_DIR "${REPO_ROOT}")
get_filename_component(GAMES_CHECK_COMPANION_ROOT_ABS "${GAMES_CHECK_COMPANION_ROOT}" ABSOLUTE BASE_DIR "${REPO_ROOT}")

# The directories of `root`, as ids: none for a root that is not there.
function(games_check_ids out root)
  file(GLOB entries CONFIGURE_DEPENDS LIST_DIRECTORIES true RELATIVE ${root} ${root}/*)
  set(ids)
  foreach(entry IN LISTS entries)
    if(IS_DIRECTORY ${root}/${entry} AND NOT entry MATCHES "^\\.")
      list(APPEND ids ${entry})
    endif()
  endforeach()
  list(SORT ids)
  set(${out} ${ids} PARENT_SCOPE)
endfunction()

games_check_ids(GAMES_CHECK_GAME_IDS_LIST ${GAMES_CHECK_GAMES_ROOT_ABS})
games_check_ids(GAMES_CHECK_COMPANION_IDS_LIST ${GAMES_CHECK_COMPANION_ROOT_ABS})
string(REPLACE ";" "," GAMES_CHECK_GAME_IDS "${GAMES_CHECK_GAME_IDS_LIST}")
string(REPLACE ";" "," GAMES_CHECK_COMPANION_IDS "${GAMES_CHECK_COMPANION_IDS_LIST}")

set(PACKED_GAMES_DIR ${CMAKE_CURRENT_BINARY_DIR}/packed_games)
set(GAMES_CHECK_SCRATCH_DIR ${CMAKE_CURRENT_BINARY_DIR}/games_check_scratch/engine)

# Every file of the games root, so editing any member of a game packs it again.
file(GLOB_RECURSE GAMES_CHECK_GAME_FILES CONFIGURE_DEPENDS ${GAMES_CHECK_GAMES_ROOT_ABS}/*)
add_custom_command(
  OUTPUT ${PACKED_GAMES_DIR}/packed.stamp
  COMMAND ${Python3_EXECUTABLE} ${HARNESS_DIR}/pack_games.py ${REPO_ROOT}/scripts/pack_game.py
          ${GAMES_CHECK_GAMES_ROOT_ABS} ${PACKED_GAMES_DIR} ${GAMES_CHECK_GAME_IDS_LIST}
  DEPENDS ${HARNESS_DIR}/pack_games.py ${REPO_ROOT}/scripts/pack_game.py ${REPO_ROOT}/scripts/fork_common.py
          ${REPO_ROOT}/lib/GameCore/ApiLevel.h ${REPO_ROOT}/assets/game-icons/names.txt ${GAMES_CHECK_GAME_FILES}
  COMMENT "Packing the games with scripts/pack_game.py")
add_custom_target(packed_games DEPENDS ${PACKED_GAMES_DIR}/packed.stamp)

set(GAMES_CHECK_DIR ${HARNESS_DIR}/games_check)

add_library(games_check_core STATIC
  ${GAMES_CHECK_DIR}/ScriptVm.cpp
  ${GAMES_CHECK_DIR}/RoundFile.cpp
  ${GAMES_CHECK_DIR}/RoundPlayer.cpp)
target_include_directories(games_check_core PUBLIC ${GAMES_CHECK_DIR})
target_link_libraries(games_check_core PUBLIC game_harness_core lua_vendored)

# The core the two installer-bound suites share: the glue and the Session pieces the linker asks for.
set(GAMES_CHECK_INSTALLER_SOURCES
  ${GAMES_CHECK_DIR}/GameCheck.cpp
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/MatchLifecycle.cpp)
set(GAMES_CHECK_INSTALLER_LIBS game_installer_src game_harness_src game_harness_core games_check_core GTest::gtest_main)
# The fixture games the engine tests copy and edit, and where pack_game.py and pack_games.py are.
set(GAMES_CHECK_COMMON_DEFINITIONS
  GAMES_CHECK_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures"
  PACK_GAME_PY="${REPO_ROOT}/scripts/pack_game.py"
  PACK_GAMES_PY="${HARNESS_DIR}/pack_games.py"
  PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")

add_executable(GamesCheckTest ${GAMES_CHECK_DIR}/GamesCheckTest.cpp ${GAMES_CHECK_INSTALLER_SOURCES})
add_dependencies(GamesCheckTest packed_games)
target_compile_definitions(GamesCheckTest PRIVATE
  ${GAMES_CHECK_COMMON_DEFINITIONS}
  GAMES_CHECK_GAMES_ROOT="${GAMES_CHECK_GAMES_ROOT_ABS}"
  GAMES_CHECK_COMPANION_ROOT="${GAMES_CHECK_COMPANION_ROOT_ABS}"
  GAMES_CHECK_GAME_IDS="${GAMES_CHECK_GAME_IDS}"
  GAMES_CHECK_COMPANION_IDS="${GAMES_CHECK_COMPANION_IDS}"
  PACKED_GAMES_DIR="${PACKED_GAMES_DIR}")
target_link_libraries(GamesCheckTest PRIVATE ${GAMES_CHECK_INSTALLER_LIBS})
gtest_discover_tests(GamesCheckTest PROPERTIES LABELS games-check)

add_executable(GamesCheckEngineTest
  ${GAMES_CHECK_DIR}/GamesCheckEngineTest.cpp
  ${GAMES_CHECK_DIR}/ScriptVmTest.cpp
  ${GAMES_CHECK_DIR}/RoundFileTest.cpp
  ${GAMES_CHECK_DIR}/BoardInsetsTest.cpp
  ${GAMES_CHECK_INSTALLER_SOURCES})
# BoardInsetsTest.cpp compiles the SDK's own BoardConfig.h, which wants the Arduino core: board_stubs/ stands in for it,
# for that one file, and the header needs a device selected (the X4 Pro, whose profile the test reads). The real header
# goes last on the path: the harness's own home_stubs/BoardConfig.h (HomeTabsTest) is in no executable of this suite.
set_source_files_properties(${GAMES_CHECK_DIR}/BoardInsetsTest.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${GAMES_CHECK_DIR}/board_stubs;${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/include"
  COMPILE_DEFINITIONS "FREEINK_DEVICE_X4PRO=1")
target_compile_definitions(GamesCheckEngineTest PRIVATE
  ${GAMES_CHECK_COMMON_DEFINITIONS}
  GAME_SCRIPT_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures"
  GAMES_CHECK_SCRATCH_DIR="${GAMES_CHECK_SCRATCH_DIR}")
target_include_directories(GamesCheckEngineTest PRIVATE ${REPO_ROOT}/test/game_script)
target_link_libraries(GamesCheckEngineTest PRIVATE ${GAMES_CHECK_INSTALLER_LIBS})
gtest_discover_tests(GamesCheckEngineTest PROPERTIES LABELS games-check)

# Sudoku's note images: `make_note_images.py --check` compares the 27 committed PNGs with what the tool generates (a swapped
# or resized image, a stray note_*.png, a NOTE_W or NOTE_H the images do not have), plain Python with the standard
# library only. It is a ctest and not a target, so the games-check job (`ctest -L games-check`) runs it with no workflow
# edit and no build step; it exists only while the tool does (a companion root without it, a scratch tree, has no test).
set(GAMES_CHECK_NOTE_IMAGES_TOOL ${GAMES_CHECK_COMPANION_ROOT_ABS}/sudoku/tools/make_note_images.py)
if(EXISTS ${GAMES_CHECK_NOTE_IMAGES_TOOL})
  add_test(NAME GamesCheckNoteImages COMMAND ${Python3_EXECUTABLE} ${GAMES_CHECK_NOTE_IMAGES_TOOL} --check)
  set_tests_properties(GamesCheckNoteImages PROPERTIES LABELS games-check)
endif()

add_executable(GamesCheckFlowTest ${GAMES_CHECK_DIR}/GamesCheckFlowTest.cpp)
# MatchSupport.h, the rig GameVmTest and this suite share.
target_include_directories(GamesCheckFlowTest PRIVATE ${HARNESS_DIR})
target_compile_definitions(GamesCheckFlowTest PRIVATE
  # The fixture game (pass-hidden) the match's VM plays from the fake card, as GameMatchHarnessTest does.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(GamesCheckFlowTest PRIVATE game_match_src games_check_core GTest::gtest_main)
gtest_discover_tests(GamesCheckFlowTest PROPERTIES LABELS games-check)

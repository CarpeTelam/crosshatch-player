# The match on the host: GameVM.cpp and GameMatchActivity.cpp, built unchanged, over the doubles
# in screen_stubs/ (FreeRTOS on threads with a controllable clock, the activity base, an
# ActivityManager and RenderLock, scripted input, a recording FreeInkUI target). Entry 4 of
# epic-install-and-launcher; the list screen and Home (entry 5) build on the same doubles.
#
# Why its own source library and not game_harness_src: the match's sources need
# screen_stubs/GfxRenderer.h (the shared renderer double plus displayBuffer and tapToLogical),
# and a source built against the shared double has another GfxRenderer layout. So every source
# this suite links is built here, against the screen doubles, from the same globs less the
# shared exclusions, keeping only GameVM, GameClock, GameRandom, and the match back in.
#
# Activity is the real one too, relocated: src/activities/Activity.h includes ActivityManager.h by a
# quoted path from its own folder, which finds the firmware's manager, so the suite builds copies of
# Activity.h, Activity.cpp, ActivityResult.h, and RenderLock.h (made at configure time, so a change
# to the real files is what the suite builds) in a folder of their own, where the same include
# falls through to screen_stubs/ActivityManager.h. That folder comes before src on the include path.
#
# I18n is the real one: the translations are generated into the build folder by
# scripts/gen_i18n.py (the tree's own generated files, if a firmware build left them, are never
# read: lib/I18n is not on the include path) and I18n.h and I18n.cpp are copied beside them.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

set(SCREEN_STUBS_DIR ${HARNESS_DIR}/screen_stubs)
set(REAL_ACTIVITY_DIR ${CMAKE_CURRENT_BINARY_DIR}/real_activity)
foreach(name Activity.h Activity.cpp ActivityResult.h RenderLock.h)
  configure_file(${REPO_ROOT}/src/activities/${name} ${REAL_ACTIVITY_DIR}/activities/${name} COPYONLY)
endforeach()
set(SCREEN_I18N_DIR ${CMAKE_CURRENT_BINARY_DIR}/screen_i18n)
file(MAKE_DIRECTORY ${SCREEN_I18N_DIR})
configure_file(${REPO_ROOT}/lib/I18n/I18n.h ${SCREEN_I18N_DIR}/I18n.h COPYONLY)
configure_file(${REPO_ROOT}/lib/I18n/I18n.cpp ${SCREEN_I18N_DIR}/I18n.cpp COPYONLY)
file(GLOB SCREEN_I18N_YAML CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
add_custom_command(
  OUTPUT ${SCREEN_I18N_DIR}/I18nKeys.h ${SCREEN_I18N_DIR}/I18nStrings.h ${SCREEN_I18N_DIR}/I18nStrings.cpp
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/gen_i18n.py ${REPO_ROOT}/lib/I18n/translations
          ${SCREEN_I18N_DIR}/
  WORKING_DIRECTORY ${REPO_ROOT}
  DEPENDS ${SCREEN_I18N_YAML} ${REPO_ROOT}/scripts/gen_i18n.py
  COMMENT "Generating I18n for the match harness")
add_library(game_match_i18n STATIC ${SCREEN_I18N_DIR}/I18n.cpp ${SCREEN_I18N_DIR}/I18nStrings.cpp)
target_include_directories(game_match_i18n PUBLIC ${SCREEN_I18N_DIR})
target_link_libraries(game_match_i18n PUBLIC crosspoint_test_common)

set(MATCH_EXCLUDE_GAMES ${HARNESS_EXCLUDE_GAMES})
list(REMOVE_ITEM MATCH_EXCLUDE_GAMES GameVM.cpp GameClock.cpp GameRandom.cpp)
# GameArena.cpp is built from screen_stubs/GameArenaDouble.cpp: the same, with a reserve a test can shrink.
list(APPEND MATCH_EXCLUDE_GAMES GameArena.cpp)
set(MATCH_EXCLUDE_ACTIVITIES ${HARNESS_EXCLUDE_ACTIVITIES})
list(REMOVE_ITEM MATCH_EXCLUDE_ACTIVITIES GameMatchActivity.cpp)
# The list screen is the next entry's; never built here, whatever the shared list says.
list(APPEND MATCH_EXCLUDE_ACTIVITIES GamesListActivity.cpp)
harness_game_sources(MATCH_SOURCES
                     EXCLUDE_GAMES ${MATCH_EXCLUDE_GAMES}
                     EXCLUDE_ACTIVITIES ${MATCH_EXCLUDE_ACTIVITIES})

add_library(game_match_src STATIC
  ${MATCH_SOURCES}
  ${REAL_ACTIVITY_DIR}/activities/Activity.cpp
  # The list screens' base classes and their button walk, real, for a list screen to build on
  # (GamesListActivity itself is entry 5's). Linked only when a suite references them.
  ${REPO_ROOT}/src/activities/UiListActivity.cpp
  ${REPO_ROOT}/src/activities/UiTabListActivity.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
  ${SCREEN_STUBS_DIR}/ScreenDoubles.cpp
  ${SCREEN_STUBS_DIR}/GameArenaDouble.cpp
  # What GameVM's Session and the match's lifecycle need beyond the shared core.
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/MatchLifecycle.cpp
  # The real FreeInkApp and dialogs the match's views are built with.
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_compile_definitions(game_match_src PUBLIC FREEINK_CAP_GAMES=1 ${HARNESS_EXTRA_DEFINES})
# Ahead of every other folder, the shared doubles' included: the screen doubles shadow
# lib/hal, src/activities, and src/components headers by their names.
target_include_directories(game_match_src BEFORE PUBLIC ${REAL_ACTIVITY_DIR} ${SCREEN_STUBS_DIR})
target_include_directories(game_match_src PUBLIC
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  ${REPO_ROOT}/lib/GameScript)
target_link_libraries(game_match_src PUBLIC
  game_harness_doubles
  game_harness_core
  game_match_i18n
  Threads::Threads
  ${HARNESS_EXTRA_LIBS})

add_executable(GameMatchHarnessTest
  GameVmTest.cpp
  GameMatchTest.cpp)
target_compile_definitions(GameMatchHarnessTest PRIVATE
  # The fixture games (tracer, timer, counter, bad-image) the tests play.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(GameMatchHarnessTest PRIVATE game_match_src GTest::gtest_main)
gtest_discover_tests(GameMatchHarnessTest)

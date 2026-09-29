# The src/games host harness: doubles for the firmware's storage, PSRAM, and
# renderer, and the game sources built against them unchanged.
#
#   stubs/HalStorage.h      a fake SD card: folder listing, failure injected per path
#   stubs/HalMemoryStub.*   HalMemory::allocatePsram over the heap, with guard bands
#   stubs/GfxRenderer.h     a recording renderer with a clip-honouring screen model
#   stubs/Logging.h         LOG_* captured in fakelog::lines
#   stubs/HalDisplay.h      the refresh-mode enum
#
# Each suite is one `*.cmake` file in this folder, included below: it adds its own
# executable(s) and never edits this file or another suite's, so a later entry adds
# a suite by adding a file. A suite links what it needs of:
#
#   game_harness_src       src/games/*.cpp and src/activities/games/*.cpp (each less its
#                          exclusion list, below), compiled with FREEINK_CAP_GAMES=1
#   game_harness_doubles   the doubles' own sources and the font code they lean on
#   game_harness_core      lib/GameScript, GameImages, and Utf8 (what those sources call)
#
# and can build its own source set with harness_game_sources.

set(HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# src/games and src/activities/games sources by glob, less the named exclusions (file
# names, without a folder), so a new source file needs no suite edit: harness_game_sources(
# <out variable> [EXCLUDE_GAMES <names>...] [EXCLUDE_ACTIVITIES <names>...]).
function(harness_game_sources out)
  cmake_parse_arguments(ARG "" "" "EXCLUDE_GAMES;EXCLUDE_ACTIVITIES" ${ARGN})
  file(GLOB games CONFIGURE_DEPENDS ${REPO_ROOT}/src/games/*.cpp)
  file(GLOB activities CONFIGURE_DEPENDS ${REPO_ROOT}/src/activities/games/*.cpp)
  foreach(name IN LISTS ARG_EXCLUDE_GAMES)
    if(NOT EXISTS ${REPO_ROOT}/src/games/${name})
      message(FATAL_ERROR "harness: ${name} is excluded but is not in src/games; drop it from the exclusion list")
    endif()
    list(REMOVE_ITEM games ${REPO_ROOT}/src/games/${name})
  endforeach()
  foreach(name IN LISTS ARG_EXCLUDE_ACTIVITIES)
    if(NOT EXISTS ${REPO_ROOT}/src/activities/games/${name})
      message(FATAL_ERROR "harness: ${name} is excluded but is not in src/activities/games; drop it from the exclusion list")
    endif()
    list(REMOVE_ITEM activities ${REPO_ROOT}/src/activities/games/${name})
  endforeach()
  set(${out} ${games} ${activities} PARENT_SCOPE)
endfunction()

# What the shared source set leaves out, and why. An entry comes off this list when
# the suite that needs the file adds the double it wants. A new firmware-only source in
# src/games lands here too: the harness build fails on it until it is listed.
set(HARNESS_EXCLUDE_GAMES
    # Arduino millis and esp_timer / esp_random: firmware clocks and entropy.
    GameClock.cpp
    GameRandom.cpp
    # FreeRTOS tasks and Arduino.h (the VM task and its watchdog).
    GameVM.cpp
    # The ESP-IDF HTTPS client and mbedtls.
    ForkReleaseProbe.cpp
    # Links every firmware component so the linker keeps them; it has no behaviour of its own.
    GamesBuildAnchor.cpp)
set(HARNESS_EXCLUDE_ACTIVITIES
    # The activity framework, UITheme, and the SDK's UI: the match and list screens on
    # the host are the next harness entries.
    GameMatchActivity.cpp
    GamesListActivity.cpp)

harness_game_sources(HARNESS_GAME_SOURCES
                     EXCLUDE_GAMES ${HARNESS_EXCLUDE_GAMES}
                     EXCLUDE_ACTIVITIES ${HARNESS_EXCLUDE_ACTIVITIES})

file(GLOB HARNESS_CORE_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/GameScript/*.cpp)
list(APPEND HARNESS_CORE_SOURCES
     ${REPO_ROOT}/lib/GameCore/GameImages.cpp
     ${REPO_ROOT}/lib/Utf8/Utf8.cpp)

set(HARNESS_INCLUDES
    # Ahead of every other folder: the doubles stand in for lib/hal and lib/GfxRenderer.
    ${HARNESS_DIR}/stubs
    ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/GameScript
    ${REPO_ROOT}/lib/GameCore
    ${REPO_ROOT}/lib/GameIcons
    ${REPO_ROOT}/lib/JsonParser
    ${REPO_ROOT}/lib/Memory
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/src
    ${REPO_ROOT}/src/games
    # ConverterBmpLayout.h, the converter's 1-bit layout the image files are built with.
    ${REPO_ROOT}/test/game_core)

add_library(game_harness_doubles STATIC
  ${HARNESS_DIR}/stubs/HalMemoryStub.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp)
target_include_directories(game_harness_doubles PUBLIC ${HARNESS_INCLUDES})
target_link_libraries(game_harness_doubles PUBLIC crosspoint_test_common)

add_library(game_harness_core STATIC ${HARNESS_CORE_SOURCES})
target_link_libraries(game_harness_core PUBLIC game_harness_doubles lua_vendored)

add_library(game_harness_src STATIC ${HARNESS_GAME_SOURCES})
# Every src/games source is one #if FREEINK_CAP_GAMES; the firmware sets it per board.
target_compile_definitions(game_harness_src PRIVATE FREEINK_CAP_GAMES=1)
target_link_libraries(game_harness_src PUBLIC game_harness_doubles)

# One suite per file.
file(GLOB HARNESS_SUITES CONFIGURE_DEPENDS ${HARNESS_DIR}/*.cmake)
foreach(suite IN LISTS HARNESS_SUITES)
  include(${suite})
endforeach()

# GfxBindingsTest counts the fills the real FrameReplay::draw makes for a frame
# (ReplayFills.h), so GameScriptTest links the replay and the doubles it draws on. Its
# Lua, GameScript, and Utf8 objects are its own; the archives add only what it lacks.
# GfxBindingsTest sees ReplayFills.h only, not the doubles' headers: the renderer double's
# global ::Color would clash with GameScript::Color under that file's using-directive.
add_library(game_harness_replay_probe STATIC ${HARNESS_DIR}/ReplayFills.cpp)
# PRIVATE: GameScriptTest links the archives but takes none of the doubles' include directories.
target_link_libraries(game_harness_replay_probe PRIVATE game_harness_src)
target_include_directories(game_harness_replay_probe INTERFACE ${HARNESS_DIR})
target_link_libraries(GameScriptTest PRIVATE game_harness_replay_probe)

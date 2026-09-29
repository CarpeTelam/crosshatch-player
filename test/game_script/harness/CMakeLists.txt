# The src/games host harness: doubles for the firmware's storage, PSRAM, and
# renderer, and the game sources built against them unchanged.
#
#   stubs/HalStorage.h      a fake SD card: folders, subtree rename, removeDir, folder
#                           listing, open modes, and failure injected per path
#   stubs/Print.h           the Print base HalFile derives from
#   stubs/HalMemoryStub.*   HalMemory::allocatePsram over the heap, with guard bands
#   stubs/GfxRenderer.h     a recording renderer with a clip-honouring screen model
#   stubs/Logging.h         LOG_* captured in fakelog::lines
#   stubs/HalDisplay.h      the refresh-mode enum
#
# HOW A LATER ENTRY EXTENDS THE HARNESS WITHOUT EDITING THIS FILE. Add files to this
# folder; never change an existing one. Two kinds, both `include()`d into this scope:
#
#   <name>.sources.cmake   Runs FIRST, before any library below is defined. It sets what
#                          the shared libraries are built from, by appending to:
#       HARNESS_EXCLUDE_GAMES        file names in src/games the shared library leaves out
#                                    (one that will not compile against the doubles, or that
#                                    the entry builds in its own suite with its own libraries)
#       HARNESS_EXCLUDE_ACTIVITIES   the same, for src/activities/games
#       HARNESS_EXTRA_INCLUDES       include folders for every harness target
#       HARNESS_EXTRA_DEFINES        compile definitions for the shared source library
#       HARNESS_EXTRA_LIBS           libraries the shared source library links
#       HARNESS_EXTRA_DOUBLE_SOURCES more sources for game_harness_doubles (a new double)
#       HARNESS_EXTRA_CORE_SOURCES   more sources for game_harness_core
#     A name that names no file is ignored, so a rename or a delete by a later entry
#     never forces an edit here; the new name is excluded in the renaming entry's own file.
#   <name>.cmake           A suite, run after the libraries: it adds its own executable(s).
#
# A suite links what it needs of:
#
#   game_harness_src       src/games/*.cpp and src/activities/games/*.cpp (each less the
#                          exclusions), compiled with FREEINK_CAP_GAMES=1 and archived, so
#                          only the objects a suite references are linked
#   game_harness_doubles   the doubles' own sources and the font code they lean on
#   game_harness_core      lib/GameScript, GameImages, and Utf8 (what those sources call)
#
# and can build its own source set with harness_game_sources. GameScriptTest's replay
# probe below is built from an explicit list, so a new glob-matched file never reaches it.

set(HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# src/games and src/activities/games sources by glob, less the named exclusions (file
# names, without a folder), so a new source file needs no suite edit: harness_game_sources(
# <out variable> [EXCLUDE_GAMES <names>...] [EXCLUDE_ACTIVITIES <names>...]). An excluded
# name that is not there is ignored.
function(harness_game_sources out)
  cmake_parse_arguments(ARG "" "" "EXCLUDE_GAMES;EXCLUDE_ACTIVITIES" ${ARGN})
  file(GLOB games CONFIGURE_DEPENDS ${REPO_ROOT}/src/games/*.cpp)
  file(GLOB activities CONFIGURE_DEPENDS ${REPO_ROOT}/src/activities/games/*.cpp)
  foreach(name IN LISTS ARG_EXCLUDE_GAMES)
    list(REMOVE_ITEM games ${REPO_ROOT}/src/games/${name})
  endforeach()
  foreach(name IN LISTS ARG_EXCLUDE_ACTIVITIES)
    list(REMOVE_ITEM activities ${REPO_ROOT}/src/activities/games/${name})
  endforeach()
  set(${out} ${games} ${activities} PARENT_SCOPE)
endfunction()

set(HARNESS_EXCLUDE_GAMES)
set(HARNESS_EXCLUDE_ACTIVITIES)
set(HARNESS_EXTRA_INCLUDES)
set(HARNESS_EXTRA_DEFINES)
set(HARNESS_EXTRA_LIBS)
set(HARNESS_EXTRA_DOUBLE_SOURCES)
set(HARNESS_EXTRA_CORE_SOURCES)

# Phase 1: what each entry adds to or takes out of the shared libraries.
file(GLOB HARNESS_SOURCE_LISTS CONFIGURE_DEPENDS ${HARNESS_DIR}/*.sources.cmake)
foreach(list IN LISTS HARNESS_SOURCE_LISTS)
  include(${list})
endforeach()

harness_game_sources(HARNESS_GAME_SOURCES
                     EXCLUDE_GAMES ${HARNESS_EXCLUDE_GAMES}
                     EXCLUDE_ACTIVITIES ${HARNESS_EXCLUDE_ACTIVITIES})

file(GLOB HARNESS_CORE_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/GameScript/*.cpp)
list(APPEND HARNESS_CORE_SOURCES
     ${REPO_ROOT}/lib/GameCore/GameImages.cpp
     ${REPO_ROOT}/lib/Utf8/Utf8.cpp
     ${HARNESS_EXTRA_CORE_SOURCES})

set(HARNESS_BASE_INCLUDES
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
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${HARNESS_EXTRA_DOUBLE_SOURCES})
target_include_directories(game_harness_doubles PUBLIC ${HARNESS_BASE_INCLUDES} ${HARNESS_EXTRA_INCLUDES})
target_link_libraries(game_harness_doubles PUBLIC crosspoint_test_common)

add_library(game_harness_core STATIC ${HARNESS_CORE_SOURCES})
target_link_libraries(game_harness_core PUBLIC game_harness_doubles lua_vendored)

add_library(game_harness_src STATIC ${HARNESS_GAME_SOURCES})
# Every src/games source is one #if FREEINK_CAP_GAMES; the firmware sets it per board.
target_compile_definitions(game_harness_src PRIVATE FREEINK_CAP_GAMES=1 ${HARNESS_EXTRA_DEFINES})
target_link_libraries(game_harness_src PUBLIC game_harness_doubles ${HARNESS_EXTRA_LIBS})

# Phase 2: one suite per file.
file(GLOB HARNESS_SUITES CONFIGURE_DEPENDS ${HARNESS_DIR}/*.cmake)
list(FILTER HARNESS_SUITES EXCLUDE REGEX "\\.sources\\.cmake$")
foreach(suite IN LISTS HARNESS_SUITES)
  include(${suite})
endforeach()

# GfxBindingsTest counts the fills the real FrameReplay::draw makes for a frame
# (ReplayFills.h), so GameScriptTest links the replay. The probe is built from an explicit
# list of the sources the replay needs, with only the base include folders, so nothing a
# later entry adds to src/games or to the shared libraries can break GameScriptTest. Its
# Lua, GameScript, GameImages, and Utf8 objects are the executable's own. GfxBindingsTest
# includes ReplayFills.h and never the renderer double, whose global ::Color would clash
# with GameScript::Color under that file's using-directive, and the doubles' include
# folders are the probe's alone (PRIVATE).
add_library(game_harness_replay_probe STATIC
  ${HARNESS_DIR}/ReplayFills.cpp
  ${REPO_ROOT}/src/games/FrameReplay.cpp
  ${REPO_ROOT}/src/games/GameIconDraw.cpp
  ${REPO_ROOT}/src/games/GameViewport.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp)
target_compile_definitions(game_harness_replay_probe PRIVATE FREEINK_CAP_GAMES=1)
target_include_directories(game_harness_replay_probe PRIVATE ${HARNESS_BASE_INCLUDES})
target_include_directories(game_harness_replay_probe INTERFACE ${HARNESS_DIR})
target_link_libraries(game_harness_replay_probe PRIVATE crosspoint_test_common)
target_link_libraries(GameScriptTest PRIVATE game_harness_replay_probe)

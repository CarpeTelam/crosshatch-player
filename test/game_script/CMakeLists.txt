# Lua is built as C from the same source list as the firmware: every .c file in lib/lua/src
# minus the -<file> entries of lib/lua/library.json's srcFilter, with the -D flags, the
# "-I <dir>" include directories (relative to lib/lua, as PlatformIO resolves them), and the
# "-include <file>" force-includes (found on those directories) from that manifest. Any other filter or flag entry stops configuration, so a manifest change the host
# build cannot mirror is noticed here instead of silently building something different.
set(LUA_DIR ${REPO_ROOT}/lib/lua)
set(LUA_MANIFEST_PATH ${LUA_DIR}/library.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LUA_MANIFEST_PATH})
file(READ ${LUA_MANIFEST_PATH} LUA_MANIFEST)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${LUA_DIR}/src/*.c)

set(LUA_DEFINES)
set(LUA_OPTIONS)
set(LUA_INCLUDES)
set(LUA_EXCLUDED_COUNT 0)
string(REGEX MATCHALL "\"[-+][^\"]*\"" LUA_ENTRIES "${LUA_MANIFEST}")
foreach(entry IN LISTS LUA_ENTRIES)
  if(entry STREQUAL "\"+<*>\"")
    continue()
  elseif(entry MATCHES "^\"-<([A-Za-z0-9_]+\\.c)>\"$")
    set(name ${CMAKE_MATCH_1})
    if(NOT EXISTS ${LUA_DIR}/src/${name})
      message(FATAL_ERROR "${LUA_MANIFEST_PATH} excludes ${name}, which is not in ${LUA_DIR}/src")
    endif()
    list(REMOVE_ITEM LUA_SOURCES ${LUA_DIR}/src/${name})
    math(EXPR LUA_EXCLUDED_COUNT "${LUA_EXCLUDED_COUNT} + 1")
  elseif(entry MATCHES "^\"-D(.+)\"$")
    list(APPEND LUA_DEFINES ${CMAKE_MATCH_1})
  elseif(entry MATCHES "^\"-I ([A-Za-z0-9_/]+)\"$")
    if(NOT IS_DIRECTORY ${LUA_DIR}/${CMAKE_MATCH_1})
      message(FATAL_ERROR "${LUA_MANIFEST_PATH} adds include directory ${CMAKE_MATCH_1}, which is not in ${LUA_DIR}")
    endif()
    list(APPEND LUA_INCLUDES ${LUA_DIR}/${CMAKE_MATCH_1})
  elseif(entry MATCHES "^\"-include ([A-Za-z0-9_.]+)\"$")
    list(APPEND LUA_OPTIONS "SHELL:-include ${CMAKE_MATCH_1}")
  else()
    message(FATAL_ERROR "${LUA_MANIFEST_PATH}: ${entry} has no host equivalent here; mirror it in this file")
  endif()
endforeach()
if(LUA_EXCLUDED_COUNT EQUAL 0)
  message(FATAL_ERROR "${LUA_MANIFEST_PATH}: no -<file> entries found in srcFilter")
endif()

# The Lua build's C-level limits, which api-level-1.txt lists as limits a game can hit;
# ApiSurfaceTest pins the list to these values.
foreach(define IN LISTS LUA_DEFINES)
  if(define MATCHES "^LUAI_MAXCCALLS=([0-9]+)$")
    set(LUA_BUILD_LUAI_MAXCCALLS ${CMAKE_MATCH_1})
  elseif(define MATCHES "^MAXCCALLS=([0-9]+)$")
    set(LUA_BUILD_MAXCCALLS ${CMAKE_MATCH_1})
  endif()
endforeach()
if(NOT DEFINED LUA_BUILD_LUAI_MAXCCALLS OR NOT DEFINED LUA_BUILD_MAXCCALLS)
  message(FATAL_ERROR "${LUA_MANIFEST_PATH}: no -DLUAI_MAXCCALLS or -DMAXCCALLS; docs/crosshatch/api-level-1.txt lists both")
endif()

add_library(lua_vendored STATIC ${LUA_SOURCES})
set_target_properties(lua_vendored PROPERTIES LINKER_LANGUAGE C)
target_include_directories(lua_vendored PUBLIC ${LUA_DIR}/src)
target_include_directories(lua_vendored PRIVATE ${LUA_INCLUDES})
target_compile_definitions(lua_vendored PRIVATE ${LUA_DEFINES})
target_compile_options(lua_vendored PRIVATE -Wall -Wextra ${LUA_OPTIONS})
# ldo.c refers to the weak luaport_memoryerror (lib/lua/port/luai_throw.h), which only
# CallGuard.cpp defines. ELF linkers leave an undefined weak symbol null; Darwin's ld64
# refuses it, so a target that links Lua without CallGuard.cpp (GameSaveStoreTest) needs
# it marked as allowed to stay undefined.
if(APPLE)
  target_link_options(lua_vendored INTERFACE "LINKER:-U,_luaport_memoryerror")
endif()

# Every GameScript source, so a new file there is built and linked by the host suite.
file(GLOB GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/GameScript/*.cpp)

add_executable(GameScriptTest
  LuaOnHostTest.cpp
  ApiSurfaceTest.cpp
  ArenaAllocatorTest.cpp
  CallGuardTest.cpp
  BlitClipTest.cpp
  CanvasClipTest.cpp
  CodecTest.cpp
  DisplayListTest.cpp
  GameIconBlitTest.cpp
  GameImageBlitTest.cpp
  GameTimerTest.cpp
  GameTouchTest.cpp
  GameViewIconsTest.cpp
  GfxBindingsTest.cpp
  HostBindingsTest.cpp
  LuaGameTest.cpp
  RefreshPolicyTest.cpp
  SandboxTest.cpp
  SessionGameTest.cpp
  SoloRoundsTest.cpp
  StoreSlotTest.cpp
  TextMetricsTest.cpp
  VmFailureTest.cpp
  ${GAME_SCRIPT_SOURCES}
  ${REPO_ROOT}/lib/GameCore/GameImages.cpp
  # GameViewIconsTest walks every state's menu.
  ${REPO_ROOT}/lib/GameCore/MatchLifecycle.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  # TextMetrics decodes with the renderer's UTF-8 reader.
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  # GameViewIconsTest lays out a real fui::optionDialog (ensureMinTouchRect, drawText).
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)

target_include_directories(GameScriptTest PRIVATE
  ${REPO_ROOT}/lib/GameScript
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/GameIcons
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Utf8
  # GameTouch.h, GameIconBlit.h, GameImageBlit.h, and GameViewIcons.h (header-only, pure) and the SDK's swipe classification GameTouch.h uses.
  ${REPO_ROOT}/src/games
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  # ApiLevelList.h, the API list reader ApiLevelTest shares, and ConverterBmpLayout.h, the image tests' converter mirror.
  ${REPO_ROOT}/test/game_core
)

# Fixture games stay under test/, never games/ (the release packs games/).
target_compile_definitions(GameScriptTest PRIVATE
  GAME_SCRIPT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  # Read at run time so this suite and scripts/game_codec_test.py share one vector file.
  CODEC_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/codec_vectors.json"
  API_LEVEL_LIST_DIR="${REPO_ROOT}/docs/crosshatch"
  LUA_BUILD_LUAI_MAXCCALLS=${LUA_BUILD_LUAI_MAXCCALLS}
  LUA_BUILD_MAXCCALLS=${LUA_BUILD_MAXCCALLS}
)

target_link_libraries(GameScriptTest PRIVATE
  crosspoint_test_common
  lua_vendored
  GTest::gtest_main
)

gtest_discover_tests(GameScriptTest)

# GameSaveStore (src/games) over the harness's fake SD card and captured log
# (harness/stubs/HalStorage.h and Logging.h): store.bin's restore rules and its
# tmp-then-rename write, and resume.bin's. The stubs stand in for HalStorage.h and
# Logging.h, so this is its own executable.
add_executable(GameSaveStoreTest
  GameSaveStoreTest.cpp
  ${REPO_ROOT}/src/games/GameSaveStore.cpp
  ${REPO_ROOT}/lib/GameScript/BlobHeader.cpp
  ${REPO_ROOT}/lib/GameScript/Codec.cpp
  ${REPO_ROOT}/lib/GameScript/StoreSlot.cpp
  ${REPO_ROOT}/lib/GameCore/Manifest.cpp
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GameSaveStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/stubs
  ${REPO_ROOT}/lib/GameScript
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src/games
)

target_compile_definitions(GameSaveStoreTest PRIVATE FREEINK_CAP_GAMES=1)

target_link_libraries(GameSaveStoreTest PRIVATE
  crosspoint_test_common
  lua_vendored
  GTest::gtest_main
)

gtest_discover_tests(GameSaveStoreTest)

# The src/games host harness (doubles for storage, PSRAM, and the renderer; its suites are
# harness/*.cmake).
add_subdirectory(harness)

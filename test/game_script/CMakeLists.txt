# Lua is built as C from the same source list as the firmware: every .c file in lib/lua/src
# minus the -<file> entries of lib/lua/library.json's srcFilter, with the -D flags from that
# manifest. Any other filter or flag entry stops configuration, so a manifest change the host
# build cannot mirror is noticed here instead of silently building something different.
set(LUA_DIR ${REPO_ROOT}/lib/lua)
set(LUA_MANIFEST_PATH ${LUA_DIR}/library.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LUA_MANIFEST_PATH})
file(READ ${LUA_MANIFEST_PATH} LUA_MANIFEST)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${LUA_DIR}/src/*.c)

set(LUA_DEFINES)
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
  else()
    message(FATAL_ERROR "${LUA_MANIFEST_PATH}: ${entry} has no host equivalent here; mirror it in this file")
  endif()
endforeach()
if(LUA_EXCLUDED_COUNT EQUAL 0)
  message(FATAL_ERROR "${LUA_MANIFEST_PATH}: no -<file> entries found in srcFilter")
endif()

add_library(lua_vendored STATIC ${LUA_SOURCES})
set_target_properties(lua_vendored PROPERTIES LINKER_LANGUAGE C)
target_include_directories(lua_vendored PUBLIC ${LUA_DIR}/src)
target_compile_definitions(lua_vendored PRIVATE ${LUA_DEFINES})
target_compile_options(lua_vendored PRIVATE -Wall -Wextra)

# Every GameScript source, so a new file there is built and linked by the host suite.
file(GLOB GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/GameScript/*.cpp)

add_executable(GameScriptTest
  GameScriptTest.cpp
  LuaOnHostTest.cpp
  ArenaAllocatorTest.cpp
  CallGuardTest.cpp
  CanvasClipTest.cpp
  CodecTest.cpp
  DisplayListTest.cpp
  LuaGameTest.cpp
  SandboxTest.cpp
  SessionGameTest.cpp
  ${GAME_SCRIPT_SOURCES}
  ${REPO_ROOT}/lib/GameCore/Roster.cpp
  ${REPO_ROOT}/lib/GameCore/Session.cpp
  ${REPO_ROOT}/lib/GameIcons/GameIcons.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GameScriptTest PRIVATE
  ${REPO_ROOT}/lib/GameScript
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/GameIcons
  ${REPO_ROOT}/lib/JsonParser
)

# Fixture games stay under test/, never games/ (the release packs games/).
target_compile_definitions(GameScriptTest PRIVATE
  GAME_SCRIPT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  # Read at run time so this suite and scripts/game_codec_test.py share one vector file.
  CODEC_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/codec_vectors.json"
)

target_link_libraries(GameScriptTest PRIVATE
  crosspoint_test_common
  lua_vendored
  GTest::gtest_main
)

gtest_discover_tests(GameScriptTest)

# Home on the host: HomeActivity.cpp and CoverGridHomeUi.cpp, built unchanged over the screen doubles.
# Entry 5 of epic-install-and-launcher, for deferred-work `## 3.6`: the cover grid's tab order against
# HomeActivity's index mapping.
#
# Neither file builds in place (the record is `## 4.4` in deferred-work.md and the plan of entry 5), so, as
# match.cmake does for Activity, the suite builds copies of them made at configure time, byte for byte, in
# a folder of its own (a change to the real files is what the suite builds):
#   - CoverGridHomeUi.h includes "UiAppHost.h" and "UITheme.h" by a sibling path, which in the
#     firmware's folder finds the real ones; in the copy's folder they fall through to home_stubs/,
#     which forwards to the screen doubles.
#   - HomeActivity.h includes "components/CoverGridHomeUi.h", which the copies' folder answers first.
# The headers the host lacks (BoardConfig.h, FsHelpers.h, RecentBooksStore.h, Epub.h, Xtc.h,
# CrossPointSettings.h, CrossPointState.h, OpdsServerStore.h, Bitmap.h, HomeCoverCache.h) are doubles in
# home_stubs/, each with the surface Home calls and nothing else. LibraryIndexFile.h and LibraryBuilder.h are the
# real headers, their few members Home calls defined in HomeStubs.cpp (the real LibraryIndexFile.cpp reads the card
# with HalFile::fileSize64, which the shared fake card does not have).

set(HOME_STUBS_DIR ${HARNESS_DIR}/home_stubs)
set(REAL_HOME_DIR ${CMAKE_CURRENT_BINARY_DIR}/real_home)
foreach(name HomeActivity.h HomeActivity.cpp)
  configure_file(${REPO_ROOT}/src/activities/home/${name} ${REAL_HOME_DIR}/activities/home/${name} COPYONLY)
endforeach()
foreach(name CoverGridHomeUi.h CoverGridHomeUi.cpp)
  configure_file(${REPO_ROOT}/src/components/${name} ${REAL_HOME_DIR}/components/${name} COPYONLY)
endforeach()
foreach(name blocks book folder library settings2 transfer)
  configure_file(${REPO_ROOT}/src/components/icons/${name}.h ${REAL_HOME_DIR}/icons/${name}.h COPYONLY)
endforeach()

# The renderer Home draws with. screen_stubs/GfxRenderer.h has no drawIcon (the cover grid's tabs are icons),
# getRegionByteSize, copyRegionToBuffer, or copyBufferToRegion (HomeActivity's cover snapshot), and the
# entries after 4 may not edit it. So the suite builds a copy of it with those four added, in the folder
# the copies above are in, which comes first on the include path, so the sources and the test built here
# see it under the same name. The additions are member functions only, with the icons they record kept
# in a function-local static and not in the object, so the class is laid out as the original is, and the
# objects of game_match_src (built against the original, linked in beside these) agree with it on
# where every member is. If the anchor below moves, this stops the configure, never the build.
set(GFX_ANCHOR "  // Forgets the recorded calls, the screen model, and the displays.")
file(READ ${HARNESS_DIR}/screen_stubs/GfxRenderer.h GFX_TEXT)
string(FIND "${GFX_TEXT}" "${GFX_ANCHOR}" GFX_ANCHOR_AT)
if(GFX_ANCHOR_AT EQUAL -1)
  message(FATAL_ERROR "home.cmake: screen_stubs/GfxRenderer.h no longer has the line it inserts drawIcon before")
endif()
string(REPLACE "\"../stubs/GfxRenderer.h\"" "\"${HARNESS_DIR}/stubs/GfxRenderer.h\"" GFX_TEXT "${GFX_TEXT}")
string(FIND "${GFX_TEXT}" "\"${HARNESS_DIR}/stubs/GfxRenderer.h\"" GFX_INCLUDE_AT)
if(GFX_INCLUDE_AT EQUAL -1)
  message(FATAL_ERROR "home.cmake: screen_stubs/GfxRenderer.h no longer includes \"../stubs/GfxRenderer.h\" by that path")
endif()
# file(GENERATE) evaluates $<...>; a header copied whole must not hold one.
string(FIND "${GFX_TEXT}" "$<" GFX_GENEX_AT)
if(NOT GFX_GENEX_AT EQUAL -1)
  message(FATAL_ERROR "home.cmake: screen_stubs/GfxRenderer.h holds a $< sequence, which file(GENERATE) would evaluate")
endif()
set(GFX_EXTRAS [==[
  // ---- added by home.cmake ----
  // One drawIcon call: the bitmap's bytes (size * size / 8), where, and how big. Kept in one list for the run
  // (drawnIcons()), not in the renderer.
  struct IconDrawn {
    std::vector<uint8_t> bitmap;
    int x;
    int y;
    int size;
  };
  static std::vector<IconDrawn>& drawnIcons() {
    static std::vector<IconDrawn> icons;
    return icons;
  }
  void drawIcon(const uint8_t bitmap[], const int x, const int y, const int size) const {
    drawnIcons().push_back({std::vector<uint8_t>(bitmap, bitmap + size * size / 8), x, y, size});
  }
  // The cover snapshot HomeActivity takes around the tile: no panel memory here, so a region holds nothing.
  size_t getRegionByteSize(int, int, const int w, const int h) const { return static_cast<size_t>((w + 7) / 8) * h; }
  bool copyRegionToBuffer(int, int, int, int, uint8_t*, size_t) const { return true; }
  bool copyBufferToRegion(int, int, int, int, const uint8_t*, size_t) const { return true; }
  // ---- end of home.cmake's additions ----

]==])
string(REPLACE "${GFX_ANCHOR}" "${GFX_EXTRAS}${GFX_ANCHOR}" GFX_TEXT "${GFX_TEXT}")
file(GENERATE OUTPUT ${REAL_HOME_DIR}/GfxRenderer.h CONTENT "${GFX_TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HARNESS_DIR}/screen_stubs/GfxRenderer.h)

add_library(game_home_src STATIC
  ${REAL_HOME_DIR}/activities/home/HomeActivity.cpp
  ${REAL_HOME_DIR}/components/CoverGridHomeUi.cpp
  ${HOME_STUBS_DIR}/HomeStubs.cpp)
target_include_directories(game_home_src BEFORE PUBLIC ${REAL_HOME_DIR} ${HOME_STUBS_DIR})
target_include_directories(game_home_src PUBLIC ${REPO_ROOT}/lib/LibraryIndex)
target_link_libraries(game_home_src PUBLIC game_match_src)

add_executable(HomeHarnessTest HomeTabsTest.cpp)
target_compile_definitions(HomeHarnessTest PRIVATE
  # MatchSupport.h, the screen fixture, reads the fixture games from here.
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(HomeHarnessTest PRIVATE game_home_src GTest::gtest_main)
gtest_discover_tests(HomeHarnessTest)

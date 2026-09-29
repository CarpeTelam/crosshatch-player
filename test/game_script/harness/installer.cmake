# GamePackageInstaller, GameRegistry, GameHash, and the package hash over the fake SD card,
# with the real ZipFile, PngToBmpConverter, and InflateStream: entry 3 of
# epic-install-and-launcher. A standalone suite: it links none of the shared libraries, since
# the real ZipFile seeks relative (HalFile::seekCur) and the shared fake card does not.
#
# The card is the shared fake (stubs/HalStorage.h) plus that one method, added to a copy of
# the header made here at configure time; the configure stops if the header no longer holds
# the line the method goes after. Move seekCur into the shared fake and this copy can go.
set(INSTALLER_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/installer_generated)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HARNESS_DIR}/stubs/HalStorage.h)
file(READ ${HARNESS_DIR}/stubs/HalStorage.h INSTALLER_FAKE_CARD)
set(INSTALLER_SEEK_ANCHOR "  bool seekSet(const size_t to) { return seek(to); }\n")
string(FIND "${INSTALLER_FAKE_CARD}" "${INSTALLER_SEEK_ANCHOR}" INSTALLER_ANCHOR_AT)
if(INSTALLER_ANCHOR_AT EQUAL -1)
  message(FATAL_ERROR "stubs/HalStorage.h no longer has `bool seekSet(...)`, where installer.cmake adds seekCur")
endif()
string(REPLACE "${INSTALLER_SEEK_ANCHOR}"
       "${INSTALLER_SEEK_ANCHOR}  bool seekCur(const int64_t offset) { return seek(static_cast<size_t>(static_cast<int64_t>(pos) + offset)); }\n"
       INSTALLER_FAKE_CARD "${INSTALLER_FAKE_CARD}")
set(INSTALLER_CARD_HEADER ${INSTALLER_GENERATED}/HalStorage.h)
set(INSTALLER_CARD_OLD "")
if(EXISTS ${INSTALLER_CARD_HEADER})
  file(READ ${INSTALLER_CARD_HEADER} INSTALLER_CARD_OLD)
endif()
if(NOT INSTALLER_CARD_OLD STREQUAL INSTALLER_FAKE_CARD)
  file(WRITE ${INSTALLER_CARD_HEADER} "${INSTALLER_FAKE_CARD}")
endif()

add_library(game_installer_src STATIC
  ${REPO_ROOT}/src/games/GamePackageInstaller.cpp
  ${REPO_ROOT}/src/games/GameRegistry.cpp
  ${REPO_ROOT}/src/games/GameHash.cpp
  ${REPO_ROOT}/src/games/GameHostCaps.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/PngToBmpConverter/PngToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
  ${REPO_ROOT}/lib/GameCore/Manifest.cpp
  ${REPO_ROOT}/lib/GameCore/GameImages.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp)
target_compile_definitions(game_installer_src PUBLIC FREEINK_CAP_GAMES=1 GAME_HASH_OPENSSL=1)
target_include_directories(game_installer_src PUBLIC
  # The card first, then the panel and FreeRTOS the converter includes, then the rest of the doubles.
  ${INSTALLER_GENERATED}
  ${HARNESS_DIR}/installer_stubs
  ${HARNESS_DIR}/stubs
  ${REPO_ROOT}/lib/GameCore
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/games
  # esp_heap_caps.h for HalMemory.cpp (its Logging.h loses to stubs/Logging.h).
  ${REPO_ROOT}/test/inflate_stream/stubs)
target_link_libraries(game_installer_src PUBLIC crosspoint_test_common OpenSSL::Crypto)

add_executable(GameInstallerTest
  GameHashTest.cpp
  GameRegistryTest.cpp
  GamePackageInstallerTest.cpp)
target_compile_definitions(GameInstallerTest PRIVATE
  PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core"
  # The fixture games the fixtures README says pack and install.
  GAME_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(GameInstallerTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(GameInstallerTest)

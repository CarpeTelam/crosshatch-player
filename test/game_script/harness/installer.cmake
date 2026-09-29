# GamePackageInstaller, GameRegistry, GameHash, and the package hash over the fake SD card,
# with the real ZipFile, PngToBmpConverter, and InflateStream: entry 3 of
# epic-install-and-launcher. A standalone suite: it builds the sources it needs itself (the
# converter wants a panel size and vTaskDelay, from installer_stubs/) over the shared fake card.

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
  # The panel and FreeRTOS the converter includes, then the shared doubles (the fake card).
  ${HARNESS_DIR}/installer_stubs
  ${HARNESS_DIR}/stubs
  ${REPO_ROOT}/lib/GameCore
  # GameIcons.h, which the installer checks a manifest's icon against (entry 7).
  ${REPO_ROOT}/lib/GameIcons
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

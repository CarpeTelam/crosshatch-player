# The packer's live output through the C++ installer. The
# only other bridge between scripts/pack_game.py and GamePackageInstaller is the committed package_vector.cpgame, which
# has no image or icon; here the host build runs the real packer on fixture folders into the build dir, and
# PackedFixturesTest installs each package with the real installer and lists it through the registry. It links
# installer.cmake's game_installer_src unchanged, and runs after it by name.
#
# To pack one more fixture, add its folder name to PACKED_FIXTURES.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PACKED_FIXTURES counter timing pack-images)
set(PACKED_FIXTURES_DIR ${CMAKE_CURRENT_BINARY_DIR}/packed_fixtures)

set(PACKED_FIXTURE_INPUTS)
set(PACKED_FIXTURE_OUTPUTS)
foreach(fixture IN LISTS PACKED_FIXTURES)
  file(GLOB_RECURSE fixture_files CONFIGURE_DEPENDS ${REPO_ROOT}/test/game_script/fixtures/${fixture}/*)
  list(APPEND PACKED_FIXTURE_INPUTS ${fixture_files})
  list(APPEND PACKED_FIXTURE_OUTPUTS ${PACKED_FIXTURES_DIR}/${fixture}.cpgame ${PACKED_FIXTURES_DIR}/${fixture}.hash)
endforeach()

add_custom_command(
  OUTPUT ${PACKED_FIXTURE_OUTPUTS}
  COMMAND ${Python3_EXECUTABLE} ${HARNESS_DIR}/pack_fixtures.py ${REPO_ROOT}/scripts/pack_game.py
          ${REPO_ROOT}/test/game_script/fixtures ${PACKED_FIXTURES_DIR} ${PACKED_FIXTURES}
  DEPENDS ${HARNESS_DIR}/pack_fixtures.py ${REPO_ROOT}/scripts/pack_game.py ${REPO_ROOT}/scripts/fork_common.py
          ${REPO_ROOT}/lib/GameCore/ApiLevel.h ${REPO_ROOT}/assets/game-icons/names.txt ${PACKED_FIXTURE_INPUTS}
  COMMENT "Packing the fixture games with scripts/pack_game.py")
add_custom_target(packed_fixtures DEPENDS ${PACKED_FIXTURE_OUTPUTS})

string(REPLACE ";" "," PACKED_FIXTURE_IDS "${PACKED_FIXTURES}")
add_executable(PackedFixturesTest PackedFixturesTest.cpp)
add_dependencies(PackedFixturesTest packed_fixtures)
target_compile_definitions(PackedFixturesTest PRIVATE
  PACKED_FIXTURES_DIR="${PACKED_FIXTURES_DIR}"
  PACKED_FIXTURE_IDS="${PACKED_FIXTURE_IDS}"
  # InstallerSupport.h's loadVector, which this suite does not call.
  PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(PackedFixturesTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(PackedFixturesTest)

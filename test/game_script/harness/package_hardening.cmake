# The installer's hardening (entry 6 of epic-install-and-launcher): ZipDirectory over bytes in memory, and
# GamePackageInstaller over the fake card with the crafted packages gen_hardening_packages.py writes (Python
# deflates, and makes the malformed zips, so the firmware's miniz never has to). It runs after installer.cmake
# by name and links entry 3's game_installer_src unchanged.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(HARDENING_PACKAGES_DIR ${CMAKE_CURRENT_BINARY_DIR}/hardening_packages)
add_custom_command(
  OUTPUT ${HARDENING_PACKAGES_DIR}/cases.txt
  COMMAND ${Python3_EXECUTABLE} ${HARNESS_DIR}/gen_hardening_packages.py ${HARDENING_PACKAGES_DIR}
  DEPENDS ${HARNESS_DIR}/gen_hardening_packages.py ${REPO_ROOT}/test/game_core/package_vectors.json
  COMMENT "Crafting the hardening suite's packages")
add_custom_target(hardening_packages DEPENDS ${HARDENING_PACKAGES_DIR}/cases.txt)

add_executable(PackageHardeningTest
  MemberGuardTest.cpp
  PackageHardeningTest.cpp
  ZipDirectoryTest.cpp)
add_dependencies(PackageHardeningTest hardening_packages)
target_compile_definitions(PackageHardeningTest PRIVATE
  HARDENING_PACKAGES_DIR="${HARDENING_PACKAGES_DIR}"
  PACKAGE_VECTOR_DIR="${REPO_ROOT}/test/game_core")
target_link_libraries(PackageHardeningTest PRIVATE game_installer_src GTest::gtest_main)
gtest_discover_tests(PackageHardeningTest)

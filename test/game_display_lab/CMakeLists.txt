# The display lab's pure parts (lib/GameDisplayLab): the SSD1677 scan LUT builder and encoding, the UC8279 tables,
# planes and lab session, the drive masks, the experiments' request layout, and the scan budget; pure C++ with no
# Arduino include. Added from test/game_core/CMakeLists.txt: the upstream touch ledger (row 3) lets
# test/CMakeLists.txt hold only the two game add_subdirectory lines.
add_executable(test_scan_lut
  test_scan_lut.cpp
  test_scan_budget.cpp
  test_mask_planes.cpp
  test_ssd1677_encoding.cpp
  test_uc8279_scan_lut.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/MaskPlanes.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/Ssd1677Encoding.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/Ssd1677ScanLut.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/UC8279ScanLut.cpp
)
target_include_directories(test_scan_lut PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
target_link_libraries(test_scan_lut PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(test_scan_lut)

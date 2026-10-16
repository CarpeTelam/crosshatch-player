# The display lab's SSD1677 scan LUT builder (lib/GameDisplayLab), pure C++ with no Arduino include. Added from
# test/game_core/CMakeLists.txt: the upstream touch ledger (row 3) lets test/CMakeLists.txt hold only the two game
# add_subdirectory lines.
add_executable(test_scan_lut
  test_scan_lut.cpp
  test_scan_budget.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/Ssd1677ScanLut.cpp
)
target_include_directories(test_scan_lut PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
target_link_libraries(test_scan_lut PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(test_scan_lut)

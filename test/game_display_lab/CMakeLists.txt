# The display lab's pure parts (lib/GameDisplayLab): the SSD1677 scan LUT builder and encoding, the drive masks, the
# experiments' request layout, the scan budget, the gray engine (model, slot, preset and its codec, slot scheduler,
# transition scene, the sweep task's request word, the panel geometry), the gray viewer's picture and decode
# arithmetic, and the UltraChip stock-path bench; pure C++ with no Arduino
# include. Added from test/game_core/CMakeLists.txt: the upstream touch ledger (row 3) lets test/CMakeLists.txt hold
# only the two game add_subdirectory lines.
add_executable(test_scan_lut
  test_scan_lut.cpp
  test_scan_budget.cpp
  test_mask_planes.cpp
  test_ssd1677_encoding.cpp
  test_gray_model.cpp
  test_gray_preset.cpp
  test_slot_scheduler.cpp
  test_stock_bench.cpp
  test_sweep_control.cpp
  test_transition_scene.cpp
  test_gray_picture.cpp
  test_image_rows.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/GrayModel.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/GrayPicture.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/GrayPreset.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/ImageRows.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/MaskPlanes.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/PanelGeometry.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/Ssd1677Encoding.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/Ssd1677ScanLut.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/StockBench.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/TransitionScene.cpp
)
target_include_directories(test_scan_lut PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
target_link_libraries(test_scan_lut PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(test_scan_lut)

# The gray viewer's reads against the real decoders (test_image_decoders.cpp): PNGdec and JPEGDEC at the firmware's
# pinned versions, built for the host as their own Linux build (__LINUX__) with the firmware's PNG buffer size
# (platformio.ini). Only when their sources are there: PlatformIO's library folder by default, which a checkout that
# has never run a firmware build (CI's host-test job) does not have; point the two paths elsewhere to build it anyway.
set(CROSSHATCH_PNGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/PNGdec/src" CACHE PATH "PNGdec's src folder")
set(CROSSHATCH_JPEGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/JPEGDEC/src" CACHE PATH "JPEGDEC's src folder")
if(EXISTS "${CROSSHATCH_PNGDEC_DIR}/PNGdec.cpp" AND EXISTS "${CROSSHATCH_JPEGDEC_DIR}/JPEGDEC.cpp")
  file(GLOB DISPLAY_LAB_ZLIB_SOURCES "${CROSSHATCH_PNGDEC_DIR}/*.c")
  add_library(display_lab_decoders STATIC
    ${CROSSHATCH_PNGDEC_DIR}/PNGdec.cpp
    ${CROSSHATCH_JPEGDEC_DIR}/JPEGDEC.cpp
    ${DISPLAY_LAB_ZLIB_SOURCES}
  )
  target_compile_definitions(display_lab_decoders PUBLIC __LINUX__ PNG_MAX_BUFFERED_PIXELS=16416)
  target_include_directories(display_lab_decoders SYSTEM PUBLIC ${CROSSHATCH_PNGDEC_DIR} ${CROSSHATCH_JPEGDEC_DIR})
  target_compile_options(display_lab_decoders PRIVATE -w)
  add_executable(test_image_decoders
    test_image_decoders.cpp
    ${REPO_ROOT}/lib/GameDisplayLab/ImageRows.cpp
  )
  target_include_directories(test_image_decoders PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
  target_link_libraries(test_image_decoders PRIVATE display_lab_decoders crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(test_image_decoders)
else()
  message(STATUS "test_image_decoders not built: no PNGdec or JPEGDEC sources at CROSSHATCH_PNGDEC_DIR, CROSSHATCH_JPEGDEC_DIR")
endif()

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
# pinned versions, built for the host as their own Linux build (__LINUX__) with the firmware's PNG buffer size, read
# from platformio.ini's build flag (its one source), and JPEGDEC with the firmware's patches (scripts/jpegdec_patches,
# applied here when they are not, as scripts/patch_jpegdec.py applies them for a firmware build). Built with
# AddressSanitizer and UndefinedBehaviorSanitizer, the decoders too, so a read past their buffers fails the test.
# Only when their sources are there: PlatformIO's library folder by default, which a checkout that has never run a
# firmware build does not have; the fork's CI (crosshatch-ci.yml's Display lab decoders job) fetches both and points the
# two paths at them.
set(CROSSHATCH_PNGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/PNGdec/src" CACHE PATH "PNGdec's src folder")
set(CROSSHATCH_JPEGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/JPEGDEC/src" CACHE PATH "JPEGDEC's src folder")
if(EXISTS "${CROSSHATCH_PNGDEC_DIR}/PNGdec.cpp" AND EXISTS "${CROSSHATCH_JPEGDEC_DIR}/JPEGDEC.cpp")
  file(STRINGS "${REPO_ROOT}/platformio.ini" DISPLAY_LAB_PNG_FLAG REGEX "-DPNG_MAX_BUFFERED_PIXELS=[0-9]+" LIMIT_COUNT 1)
  string(REGEX MATCH "PNG_MAX_BUFFERED_PIXELS=([0-9]+)" DISPLAY_LAB_PNG_MATCH "${DISPLAY_LAB_PNG_FLAG}")
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "platformio.ini has no -DPNG_MAX_BUFFERED_PIXELS=<n> build flag for test_image_decoders")
  endif()
  set(DISPLAY_LAB_PNG_MAX_BUFFERED_PIXELS ${CMAKE_MATCH_1})

  # The firmware's JPEGDEC patches: each already applied (it reverses cleanly), else applied now, else a stop.
  get_filename_component(DISPLAY_LAB_JPEGDEC_ROOT "${CROSSHATCH_JPEGDEC_DIR}" DIRECTORY)
  file(GLOB DISPLAY_LAB_JPEGDEC_PATCHES "${REPO_ROOT}/scripts/jpegdec_patches/*.patch")
  list(SORT DISPLAY_LAB_JPEGDEC_PATCHES)
  find_program(DISPLAY_LAB_GIT git REQUIRED)
  foreach(patch IN LISTS DISPLAY_LAB_JPEGDEC_PATCHES)
    execute_process(COMMAND ${DISPLAY_LAB_GIT} apply --check --reverse "${patch}"
                    WORKING_DIRECTORY "${DISPLAY_LAB_JPEGDEC_ROOT}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT applied EQUAL 0)
      execute_process(COMMAND ${DISPLAY_LAB_GIT} apply "${patch}"
                      WORKING_DIRECTORY "${DISPLAY_LAB_JPEGDEC_ROOT}" RESULT_VARIABLE patched)
      if(NOT patched EQUAL 0)
        message(FATAL_ERROR "JPEGDEC patch ${patch} neither applied nor applies at ${DISPLAY_LAB_JPEGDEC_ROOT}")
      endif()
      message(STATUS "Applied JPEGDEC patch ${patch}")
    endif()
  endforeach()

  set(DISPLAY_LAB_SANITIZE -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  file(GLOB DISPLAY_LAB_ZLIB_SOURCES "${CROSSHATCH_PNGDEC_DIR}/*.c")
  add_library(display_lab_decoders STATIC
    ${CROSSHATCH_PNGDEC_DIR}/PNGdec.cpp
    ${CROSSHATCH_JPEGDEC_DIR}/JPEGDEC.cpp
    ${DISPLAY_LAB_ZLIB_SOURCES}
  )
  target_compile_definitions(display_lab_decoders PUBLIC
    __LINUX__ PNG_MAX_BUFFERED_PIXELS=${DISPLAY_LAB_PNG_MAX_BUFFERED_PIXELS})
  target_include_directories(display_lab_decoders SYSTEM PUBLIC ${CROSSHATCH_PNGDEC_DIR} ${CROSSHATCH_JPEGDEC_DIR})
  # The decoders load and store words at unaligned addresses by design (their Linux build allows it, as x86 does):
  # that one UBSan check is off for their sources.
  target_compile_options(display_lab_decoders PRIVATE -w ${DISPLAY_LAB_SANITIZE} -fno-sanitize=alignment,shift)
  add_executable(test_image_decoders
    test_image_decoders.cpp
    ${REPO_ROOT}/lib/GameDisplayLab/ImageRows.cpp
  )
  target_include_directories(test_image_decoders PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
  target_compile_options(test_image_decoders PRIVATE ${DISPLAY_LAB_SANITIZE})
  target_link_options(test_image_decoders PRIVATE -fsanitize=address,undefined)
  target_link_libraries(test_image_decoders PRIVATE display_lab_decoders crosspoint_test_common GTest::gtest_main)
  # Every decode is bounded in reads (a spin fails at once); the timeout catches a spin outside the read callbacks.
  gtest_discover_tests(test_image_decoders PROPERTIES TIMEOUT 300 LABELS display-lab-decoders)
else()
  message(STATUS "test_image_decoders not built: no PNGdec or JPEGDEC sources at CROSSHATCH_PNGDEC_DIR, CROSSHATCH_JPEGDEC_DIR")
endif()

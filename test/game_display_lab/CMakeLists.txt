# The display lab's pure parts (lib/GameDisplayLab): the SSD1677 scan LUT builder and encoding, the UC8279 and UC8179
# tables, planes and lab sessions, the drive masks, the experiments' request layout, the scan budget, the gray engine
# (model, slot, preset and its codec, slot scheduler, transition scene, the sweep task's request word and its end
# against its owner's give-up, the panel geometry), the gray viewer's picture and decode arithmetic, and the UltraChip
# stock-path bench; pure C++ with no Arduino include. Added from test/game_core/CMakeLists.txt: the upstream touch
# ledger (row 3) lets test/CMakeLists.txt hold only the two game add_subdirectory lines.
add_executable(test_scan_lut
  test_scan_lut.cpp
  test_scan_budget.cpp
  test_mask_planes.cpp
  test_ssd1677_encoding.cpp
  test_uc8279_scan_lut.cpp
  test_uc8179_scan_lut.cpp
  test_gray_model.cpp
  test_gray_preset.cpp
  test_slot_scheduler.cpp
  test_stock_bench.cpp
  test_sweep_control.cpp
  test_transition_scene.cpp
  test_gray_picture.cpp
  test_image_rows.cpp
  test_task_handoff.cpp
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
  ${REPO_ROOT}/lib/GameDisplayLab/UC8179ScanLut.cpp
  ${REPO_ROOT}/lib/GameDisplayLab/UC8279ScanLut.cpp
)
target_include_directories(test_scan_lut PRIVATE ${REPO_ROOT}/lib/GameDisplayLab)
target_link_libraries(test_scan_lut PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(test_scan_lut)

# The gray viewer's reads against the real decoders (test_image_decoders.cpp): PNGdec and JPEGDEC at the firmware's
# pinned versions, built for the host as their own Linux build (__LINUX__) with the firmware's PNG buffer size, read
# from platformio.ini's build flag (its one source), and JPEGDEC as the device builds it (a 32-bit bit reader, no SSE:
# jpegdec_as_device.h), each with the x4pro firmware's patches (upstream's scripts/jpegdec_patches, then the lab's
# scripts/lab_decoder_patches/<name>) applied to a copy of its sources in this build tree, never to PlatformIO's library
# folder (the firmware build patches that one: scripts/patch_jpegdec.py, scripts/patch_lab_decoders.py). Built with AddressSanitizer and UndefinedBehaviorSanitizer, the
# decoders too, so a read past their objects fails the test (test_image_decoders.cpp's canaries, and a poisoned guard
# after JPEGDEC's file buffer, watch inside them). Only when their sources are there: PlatformIO's library folder by
# default, which a checkout that has never run a firmware build does not have; the fork's CI (crosshatch-ci.yml's
# Display lab decoders job) fetches both and points the two paths at them.
set(CROSSHATCH_PNGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/PNGdec/src" CACHE PATH "PNGdec's src folder")
set(CROSSHATCH_JPEGDEC_DIR "${REPO_ROOT}/.pio/libdeps/x4pro/JPEGDEC/src" CACHE PATH "JPEGDEC's src folder")
if(EXISTS "${CROSSHATCH_PNGDEC_DIR}/PNGdec.cpp" AND EXISTS "${CROSSHATCH_JPEGDEC_DIR}/JPEGDEC.cpp")
  file(STRINGS "${REPO_ROOT}/platformio.ini" DISPLAY_LAB_PNG_FLAG REGEX "-DPNG_MAX_BUFFERED_PIXELS=[0-9]+" LIMIT_COUNT 1)
  string(REGEX MATCH "PNG_MAX_BUFFERED_PIXELS=([0-9]+)" DISPLAY_LAB_PNG_MATCH "${DISPLAY_LAB_PNG_FLAG}")
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "platformio.ini has no -DPNG_MAX_BUFFERED_PIXELS=<n> build flag for test_image_decoders")
  endif()
  set(DISPLAY_LAB_PNG_MAX_BUFFERED_PIXELS ${CMAKE_MATCH_1})

  # A decoder's sources copied to <build>/<name>/src on every configure, then the firmware's patches from each folder
  # given (a missing or empty folder is a stop), in order: each already in the sources (it reverses cleanly:
  # PlatformIO's folder after a firmware build) is left, else applied to the copy, else a stop. git applies them as a
  # plain patch tool: GIT_CEILING_DIRECTORIES, the copy's parent, keeps it from taking this build tree's repository.
  # `out` is set to the copy's src folder. The configure reruns when a patch, a decoder source or platformio.ini
  # changes (CMAKE_CONFIGURE_DEPENDS), so an incremental build never keeps a stale copy.
  find_program(DISPLAY_LAB_GIT git REQUIRED)
  set(DISPLAY_LAB_GIT_APPLY ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
                            ${DISPLAY_LAB_GIT} apply)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/platformio.ini")
  function(display_lab_patched_copy name from out)
    set(root "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    file(REMOVE_RECURSE "${root}")
    file(COPY "${from}" DESTINATION "${root}")
    get_filename_component(leaf "${from}" NAME)
    if(NOT leaf STREQUAL "src")
      file(RENAME "${root}/${leaf}" "${root}/src")
    endif()
    file(GLOB_RECURSE sources LIST_DIRECTORIES false "${from}/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${sources})
    set(patches "")
    foreach(folder IN LISTS ARGN)
      if(NOT IS_DIRECTORY "${folder}")
        message(FATAL_ERROR "${name} patch folder ${folder} is missing")
      endif()
      file(GLOB folder_patches "${folder}/*.patch")
      if(NOT folder_patches)
        message(FATAL_ERROR "${name} patch folder ${folder} has no patches")
      endif()
      list(SORT folder_patches)
      list(APPEND patches ${folder_patches})
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${folder_patches})
    endforeach()
    # A retired lab patch the source still carries (PlatformIO's copy from an earlier build of this branch) is taken out
    # of the copy first; one that cannot be is a stop naming the folder to delete.
    file(GLOB retired "${REPO_ROOT}/scripts/lab_decoder_patches/retired/${name}/*.patch")
    list(SORT retired)
    if(retired)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${retired})
    endif()
    foreach(patch IN LISTS retired)
      execute_process(COMMAND ${DISPLAY_LAB_GIT_APPLY} --check --reverse "${patch}"
                      WORKING_DIRECTORY "${root}" RESULT_VARIABLE carried OUTPUT_QUIET ERROR_QUIET)
      if(carried EQUAL 0)
        execute_process(COMMAND ${DISPLAY_LAB_GIT_APPLY} --reverse "${patch}" WORKING_DIRECTORY "${root}"
                        RESULT_VARIABLE undone)
        if(NOT undone EQUAL 0)
          message(FATAL_ERROR "${from} carries the retired ${name} patch ${patch}; delete its library folder")
        endif()
        message(STATUS "Took the retired ${name} patch ${patch} out of the build tree's copy")
      endif()
    endforeach()
    foreach(patch IN LISTS patches)
      execute_process(COMMAND ${DISPLAY_LAB_GIT_APPLY} --check --reverse "${patch}"
                      WORKING_DIRECTORY "${root}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
      if(NOT applied EQUAL 0)
        execute_process(COMMAND ${DISPLAY_LAB_GIT_APPLY} "${patch}" WORKING_DIRECTORY "${root}" RESULT_VARIABLE patched)
        if(NOT patched EQUAL 0)
          message(FATAL_ERROR "${name} patch ${patch} neither in ${from} nor applies to it; delete its library folder")
        endif()
        message(STATUS "Applied ${name} patch ${patch} to the build tree's copy")
      endif()
    endforeach()
    set(${out} "${root}/src" PARENT_SCOPE)
  endfunction()
  display_lab_patched_copy(pngdec "${CROSSHATCH_PNGDEC_DIR}" DISPLAY_LAB_PNGDEC_SRC
                           "${REPO_ROOT}/scripts/lab_decoder_patches/pngdec")
  display_lab_patched_copy(jpegdec "${CROSSHATCH_JPEGDEC_DIR}" DISPLAY_LAB_JPEGDEC_SRC
                           "${REPO_ROOT}/scripts/jpegdec_patches" "${REPO_ROOT}/scripts/lab_decoder_patches/jpegdec")
  # Test-only layout, in the copy alone: 64 bytes after JPEGDEC's file buffer, which test_image_decoders.cpp poisons
  # for AddressSanitizer, so a header parse or decode that reads or writes just past the buffer inside the object
  # (where the sanitizer cannot otherwise see) fails the test. The device's object is unchanged.
  file(READ "${DISPLAY_LAB_JPEGDEC_SRC}/JPEGDEC.h" DISPLAY_LAB_JPEGDEC_H)
  set(DISPLAY_LAB_FILE_BUF "uint8_t ucFileBuf[JPEG_FILE_BUF_SIZE]; // holds temp data and pixel stack")
  string(FIND "${DISPLAY_LAB_JPEGDEC_H}" "${DISPLAY_LAB_FILE_BUF}" DISPLAY_LAB_FILE_BUF_AT)
  if(DISPLAY_LAB_FILE_BUF_AT EQUAL -1)
    message(FATAL_ERROR "JPEGDEC.h has no '${DISPLAY_LAB_FILE_BUF}' for the guard after it")
  endif()
  string(REPLACE "${DISPLAY_LAB_FILE_BUF}" "${DISPLAY_LAB_FILE_BUF}\n    uint8_t ucGuard[64]; // test-only: poisoned"
                 DISPLAY_LAB_JPEGDEC_H "${DISPLAY_LAB_JPEGDEC_H}")
  file(WRITE "${DISPLAY_LAB_JPEGDEC_SRC}/JPEGDEC.h" "${DISPLAY_LAB_JPEGDEC_H}")

  # The lab JPEGDEC patch's block margin (JPEG_BLOCK_MARGIN, defined there unless already defined) is a variable here,
  # holding the patch's value, so the margin test (ImageDecoders.TheBlockMarginsDerivedMinimumHolds) can lower it; the
  # device's is the patch's constant.
  file(READ "${REPO_ROOT}/scripts/lab_decoder_patches/jpegdec/crosshatch-0001-refill-between-blocks-stop-an-overrun.patch"
       DISPLAY_LAB_JPEGDEC_PATCH)
  string(REGEX MATCH "#define JPEG_BLOCK_MARGIN ([0-9]+)" DISPLAY_LAB_MARGIN_LINE "${DISPLAY_LAB_JPEGDEC_PATCH}")
  if(NOT DISPLAY_LAB_MARGIN_LINE)
    message(FATAL_ERROR "the lab JPEGDEC patch has no '#define JPEG_BLOCK_MARGIN <n>' for the margin test")
  endif()
  set(DISPLAY_LAB_BLOCK_MARGIN ${CMAKE_MATCH_1})

  set(DISPLAY_LAB_SANITIZE -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  file(GLOB DISPLAY_LAB_ZLIB_SOURCES "${DISPLAY_LAB_PNGDEC_SRC}/*.c")
  add_library(display_lab_decoders STATIC
    ${DISPLAY_LAB_PNGDEC_SRC}/PNGdec.cpp
    jpegdec_as_device.cpp
    ${DISPLAY_LAB_ZLIB_SOURCES}
  )
  target_compile_definitions(display_lab_decoders PUBLIC
    __LINUX__ PNG_MAX_BUFFERED_PIXELS=${DISPLAY_LAB_PNG_MAX_BUFFERED_PIXELS})
  target_include_directories(display_lab_decoders SYSTEM PUBLIC ${DISPLAY_LAB_PNGDEC_SRC} ${DISPLAY_LAB_JPEGDEC_SRC})
  # Three UBSan checks are off for the decoders' own sources, each for what they do by design, as on the device:
  # - alignment: both load and store whole words at unaligned addresses (zlib's inflate.c 1324 in PNGdec; JPEGDEC with
  #   ALLOWS_UNALIGNED, which the ESP32 build sets), which the S3 and x86 allow;
  # - shift, JPEGDEC only: JPEGMakeHuffTables shifts a code right by (bit length - 5), negative for codes of under 5
  #   bits (jpeg.inl 1115), every JPEG's; the result is never used for them. GCC reports a negative exponent even with
  #   only shift-exponent turned off, so the shift group goes, for this one source.
  # - signed-integer-overflow, JPEGDEC only: its IDCT multiplies coefficients by quantisation values in 32-bit ints
  #   (jpeg.inl 2673 and on), which a corrupt table or scan overflows; it wraps on the device, a wrong pixel, no more.
  # Every other UBSan check, and all of AddressSanitizer, stays on, and the test's own sources keep them all.
  target_compile_options(display_lab_decoders PRIVATE -w ${DISPLAY_LAB_SANITIZE} -fno-sanitize=alignment)
  set_source_files_properties(jpegdec_as_device.cpp PROPERTIES COMPILE_OPTIONS "-fno-sanitize=shift,signed-integer-overflow"
                              COMPILE_DEFINITIONS "JPEG_BLOCK_MARGIN=crosshatchJpegBlockMargin;CROSSHATCH_LAB_BLOCK_MARGIN=${DISPLAY_LAB_BLOCK_MARGIN}")
  # The only test binary that includes JPEGDEC: test_image_decoders, through jpegdec_as_device.h and the library's
  # jpegdec_as_device.cpp, both of which static_assert REGISTER_WIDTH == 32, so every decoder test runs the device's
  # 32-bit bit reader (the host's own build would pick 64).
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

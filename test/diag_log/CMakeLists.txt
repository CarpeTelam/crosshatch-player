# The diagnostics log's host suite (lib/DiagLog, epic-play-nearby entry 6.19): the line buffer and the writer, which are
# header-only, so the suite compiles the same code the device runs. The device files (lib/DiagLog/DiagLogDevice.cpp and
# src/diagnostics/DiagLogStorage.cpp) are not here: they need FreeRTOS and Storage, and no CI job builds them (the define
# stays out of CI); a manual define-on `pio run`, recorded in the story's plan, is their check.
find_package(Threads REQUIRED)

add_executable(DiagLogTest
  DiagLogBufferTest.cpp
  DiagLogWriterTest.cpp
)

target_include_directories(DiagLogTest PRIVATE
  ${REPO_ROOT}/lib/DiagLog
)

# DiagLogWriterTest reads the device file and the SDK's storage sources to pin what its fake file store stands in for.
target_compile_definitions(DiagLogTest PRIVATE
  REPO_ROOT_DIR="${REPO_ROOT}"
)

target_link_libraries(DiagLogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  Threads::Threads
)

gtest_discover_tests(DiagLogTest)

# resume.bin and resuming a solo match: entry 11 of epic-install-and-launcher. Session::restore
# and the snapshot mailbox (ResumeSessionTest), and the match, its VM, and GameSaveStore over the
# screen doubles (ResumeMatchTest), which build on match.cmake's game_match_src. The GameVM and
# GameMatchActivity tests of entry 4 stay in GameMatchHarnessTest, unedited.
add_executable(ResumeHarnessTest
  ResumeSessionTest.cpp
  ResumeMatchTest.cpp
  # MatchPersistence on its own (fake clock); seedResume is covered through ResumeMatchTest's Continue tests.
  MatchPersistenceTest.cpp)
target_compile_definitions(ResumeHarnessTest PRIVATE
  MATCH_FIXTURES_DIR="${REPO_ROOT}/test/game_script/fixtures")
target_link_libraries(ResumeHarnessTest PRIVATE game_match_src GTest::gtest_main)
gtest_discover_tests(ResumeHarnessTest)

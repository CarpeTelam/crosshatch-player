# GameLink (the link task) and NearbySession (the link side of a nearby match) on the host, over FakeLink on real
# threads (entry 8 of epic-play-nearby): the task and its join (GameLinkTest.cpp), the PSRAM pool
# (GameLinkStoreTest.cpp), the LINK lines (GameLinkLogTest.cpp), the lobby-to-round flows and the handoffs
# (NearbySessionTest.cpp), and leave (NearbyLeaveTest.cpp).
#
# GameLink.cpp and NearbySession.cpp are built against the screen doubles (FreeRTOS on threads with a clock the test
# moves, a thread-safe log, the RenderLock double): they include freertos/task.h, so nearby_session.sources.cmake keeps
# them out of the shared library. Since entry 9 of epic-play-nearby GameVM.cpp calls NearbySession, so they are in
# game_match_src (match.cmake) with GameVM, GameLinkStore.cpp, and Session.cpp, which this links for them and for the
# RenderLock and ActivityManager doubles; nothing here lists them again.

add_executable(NearbySessionHarnessTest
  GameLinkTest.cpp
  GameLinkStoreTest.cpp
  GameLinkLogTest.cpp
  NearbySessionTest.cpp
  NearbyLeaveTest.cpp)
target_link_libraries(NearbySessionHarnessTest PRIVATE game_match_src GTest::gtest_main)
gtest_discover_tests(NearbySessionHarnessTest)

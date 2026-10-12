# GameLink (the link task) and NearbySession (the link side of a nearby match) on the host, over FakeLink on real
# threads (entry 8 of epic-play-nearby): the task and its join (GameLinkTest.cpp), the PSRAM pool
# (GameLinkStoreTest.cpp), the LINK lines (GameLinkLogTest.cpp), the lobby-to-round flows and the handoffs
# (NearbySessionTest.cpp), and leave (NearbyLeaveTest.cpp).
#
# GameLink.cpp and NearbySession.cpp are built here, against the screen doubles (FreeRTOS on threads with a clock the
# test moves, a thread-safe log, the RenderLock double): they include freertos/task.h, so nearby_session.sources.cmake
# keeps them out of the shared library. GameLinkStore.cpp and Session.cpp are already in game_match_src, which this
# links for the RenderLock and ActivityManager doubles, and nothing here lists them again.

find_package(Threads REQUIRED)

add_library(game_nearby_src STATIC
  ${REPO_ROOT}/src/games/GameLink.cpp
  ${REPO_ROOT}/src/games/NearbySession.cpp)
target_compile_definitions(game_nearby_src PUBLIC FREEINK_CAP_GAMES=1)
target_link_libraries(game_nearby_src PUBLIC game_match_src Threads::Threads)

add_executable(NearbySessionHarnessTest
  GameLinkTest.cpp
  GameLinkStoreTest.cpp
  GameLinkLogTest.cpp
  NearbySessionTest.cpp
  NearbyLeaveTest.cpp)
target_link_libraries(NearbySessionHarnessTest PRIVATE game_nearby_src GTest::gtest_main)
gtest_discover_tests(NearbySessionHarnessTest)

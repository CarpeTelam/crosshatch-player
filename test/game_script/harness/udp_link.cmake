# The simulator's UdpLink on the host (entry 16 of epic-play-nearby): the link contract over two and three real
# sockets on loopback, eight links and a ninth refused, datagrams a link must drop (written by a raw socket that stands
# for another process), the port base read from the environment, and a lobby handshake and a round over two
# NearbySessions on UdpLinks (UdpLinkTest.cpp).
#
# UdpLink.cpp compiles only with SIMULATOR defined, which the shared libraries do not set (udp_link.sources.cmake keeps
# it out of them), so it is built here into its own library with SIMULATOR=1. It is built against the screen doubles
# (the logging double takes a lock; game_match_src's include folders come first), as the other nearby suites are.

find_package(Threads REQUIRED)

add_library(udp_link_src STATIC ${REPO_ROOT}/src/games/UdpLink.cpp)
target_compile_definitions(udp_link_src PRIVATE SIMULATOR=1 FREEINK_CAP_GAMES=1)
target_link_libraries(udp_link_src PUBLIC game_match_src)

add_executable(UdpLinkHarnessTest UdpLinkTest.cpp)
target_link_libraries(UdpLinkHarnessTest PRIVATE udp_link_src game_match_src game_harness_core GTest::gtest_main
                      Threads::Threads)
gtest_discover_tests(UdpLinkHarnessTest)

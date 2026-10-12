# The ESP-NOW link adapter and the Nearby precondition probe on the host (entry 5 of epic-play-nearby): the receive
# ring (FrameRingTest.cpp), the probe's decision (NearbyProbeTest.cpp), and the link's logic over a fake radio and a
# fake air, including the contract every ILink passes (EspNowLinkTest.cpp). All three are header-only code in
# src/games (EspNowLink.h, FrameRing.h, NearbyProbe.h); the driver that calls esp_now.h is device-only and outside the
# harness (esp_now_link.sources.cmake), so what the host cannot show stays with entry 11's device run.

find_package(Threads REQUIRED)

add_executable(EspNowLinkHarnessTest
  FrameRingTest.cpp
  NearbyProbeTest.cpp
  EspNowLinkTest.cpp)

target_link_libraries(EspNowLinkHarnessTest PRIVATE
  game_harness_doubles
  GTest::gtest_main
  Threads::Threads
)

gtest_discover_tests(EspNowLinkHarnessTest)

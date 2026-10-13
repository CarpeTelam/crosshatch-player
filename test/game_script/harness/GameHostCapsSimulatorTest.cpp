#include <gtest/gtest.h>

#include "ApiLevel.h"
#include "GameHostCaps.h"

// GameHostCaps.cpp built with SIMULATOR (game_host_caps_simulator.cmake): the simulator plays nearby over UdpLink, so
// it reports nearby on whatever the device build says (HostCapsValues::NEARBY_BUILT is on since entry 11; the simulator
// does not depend on it). The rest of HostCaps is as on a device. This build defines no device, so it stands for the
// simulator on any board; the X4 Pro and Sticky simulators are pinned in GameHostCapsBoardsTest (entry 20).

namespace {

TEST(GameHostCapsSimulatorTest, NearbyIsOnAndTheDeviceBuildAgrees) {
  EXPECT_TRUE(gameHostCaps().nearby);
  EXPECT_TRUE(HostCapsValues::NEARBY_BUILT) << "the X4 Pro device build plays nearby over EspNowLink";
}

TEST(GameHostCapsSimulatorTest, TheOtherFieldsAreAsOnADevice) {
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.api, API_LEVEL);
  EXPECT_EQ(caps.minApi, API_MIN_LEVEL);
  EXPECT_EQ(caps.maxSeats, HostCapsValues::MAX_SEATS);
  EXPECT_EQ(caps.maxSeats, 2);
  EXPECT_TRUE(caps.pass);
  EXPECT_EQ(caps.pass, HostCapsValues::PASS);
}

}  // namespace

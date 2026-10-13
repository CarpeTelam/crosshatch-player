#include <gtest/gtest.h>

#include "ApiLevel.h"
#include "GameHostCaps.h"

// GameHostCaps.cpp built with SIMULATOR (game_host_caps_simulator.cmake): the simulator plays nearby over UdpLink, so
// it reports nearby on although nothing is built for the device radio yet. The rest of HostCaps is as on a device.

namespace {

TEST(GameHostCapsSimulatorTest, NearbyIsOnThoughNearbyIsNotBuilt) {
  EXPECT_FALSE(HostCapsValues::NEARBY_BUILT);
  EXPECT_TRUE(gameHostCaps().nearby);
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

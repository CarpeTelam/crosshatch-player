// NearbyProbe::decide (src/games/NearbyProbe.h): the pure decision behind Play Nearby's precondition (epic R8). The
// device reader, read(), is outside the harness (NearbyProbe.cpp calls WiFi and HalMemory), so the readings are the
// test's; what read() returns on a device is entry 11's check.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "NearbyProbe.h"

using NearbyProbe::decide;
using NearbyProbe::MIN_INTERNAL_HEAP_BYTES;
using NearbyProbe::Readings;
using NearbyProbe::Verdict;

static_assert(MIN_INTERNAL_HEAP_BYTES == 102400, "100 KiB: R8's 'internal heap under 100 KB'");
static_assert(decide(Readings{false, MIN_INTERNAL_HEAP_BYTES}).canOpen());
static_assert(!decide(Readings{false, MIN_INTERNAL_HEAP_BYTES - 1}).canOpen());
static_assert(!decide(Readings{true, MIN_INTERNAL_HEAP_BYTES}).canOpen());
static_assert(decide(Readings{false, 0}).lowMemory);
static_assert(!decide(Readings{false, SIZE_MAX}).lowMemory);

TEST(NearbyProbeTest, TheBoundaryIsExactlyOneHundredKiB) {
  EXPECT_TRUE(decide(Readings{false, 102399}).lowMemory);
  EXPECT_FALSE(decide(Readings{false, 102400}).lowMemory);
  EXPECT_FALSE(decide(Readings{false, 102401}).lowMemory);
}

TEST(NearbyProbeTest, WifiInUseIsTheReadingNotTheHeap) {
  for (const size_t heap : {size_t{0}, size_t{102399}, size_t{102400}, SIZE_MAX}) {
    EXPECT_TRUE(decide(Readings{true, heap}).wifiInUse) << "heap " << heap;
    EXPECT_FALSE(decide(Readings{false, heap}).wifiInUse) << "heap " << heap;
  }
}

TEST(NearbyProbeTest, EveryCombinationReportsBothFactsAndPicksNoPriority) {
  struct Case {
    bool wifi;
    size_t heap;
    bool lowMemory;
    bool canOpen;
  };
  const Case cases[] = {
      {false, 102400, false, true},   {false, 102399, true, false}, {true, 102400, false, false},
      {true, 102399, true, false},  // both causes at once: both flags set, the lobby entry picks the message
      {true, 0, true, false},         {false, 0, true, false},      {false, SIZE_MAX, false, true},
      {true, SIZE_MAX, false, false},
  };
  for (const Case& c : cases) {
    const Verdict v = decide(Readings{c.wifi, c.heap});
    EXPECT_EQ(v.wifiInUse, c.wifi) << "wifi " << c.wifi << " heap " << c.heap;
    EXPECT_EQ(v.lowMemory, c.lowMemory) << "wifi " << c.wifi << " heap " << c.heap;
    EXPECT_EQ(v.canOpen(), c.canOpen) << "wifi " << c.wifi << " heap " << c.heap;
  }
}

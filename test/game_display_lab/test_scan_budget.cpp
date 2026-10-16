// The display lab's scan bookkeeping (lib/GameDisplayLab/ScanBudget) and the frame-period experiment's scan order
// (Ssd1677ScanLut::framePeriodScan): the plan's matrix rows for the session cap, the timeout, and the nine scans.

#include <gtest/gtest.h>

#include "ScanBudget.h"
#include "Ssd1677ScanLut.h"

namespace {

using namespace GameDisplayLab;

constexpr uint32_t CAP = 2400;
constexpr uint32_t TIMEOUT_US = 15000UL * 1000UL;  // the lab's SCAN_TIMEOUT_MS
constexpr uint32_t FLOOR_US = 500;

TEST(ScanBudget, CapForcesACleanAfterTheConfiguredScans) {
  ScanBudget budget(CAP, TIMEOUT_US, FLOOR_US);
  for (uint32_t i = 0; i < CAP - 1; ++i) {
    EXPECT_FALSE(budget.cleanDue()) << "scan " << i;
    EXPECT_EQ(budget.record(1000), ScanBudget::Outcome::Done);
  }
  EXPECT_FALSE(budget.cleanDue());
  budget.record(1000);  // the 2,400th scan since the last clean
  EXPECT_TRUE(budget.cleanDue());
  EXPECT_EQ(budget.sinceClean(), CAP);
  budget.cleaned();
  EXPECT_FALSE(budget.cleanDue());
  EXPECT_EQ(budget.sinceClean(), 0u);
  EXPECT_EQ(budget.total(), CAP);  // the total survives a clean
}

TEST(ScanBudget, ScanOverTheTimeoutTimesOut) {
  ScanBudget budget(CAP, TIMEOUT_US, FLOOR_US);
  EXPECT_EQ(budget.record(TIMEOUT_US), ScanBudget::Outcome::Done);  // exactly the limit is fine
  EXPECT_EQ(budget.record(TIMEOUT_US + 1), ScanBudget::Outcome::TimedOut);
  EXPECT_EQ(budget.total(), 2u);  // a timed-out scan still counts: it reached the panel
  EXPECT_EQ(budget.sinceClean(), 2u);
}

// A call the facade dropped comes back in microseconds: no activation ran, so the time is no measurement.
TEST(ScanBudget, ScanUnderTheFloorIsDroppedAndStillCounted) {
  ScanBudget budget(CAP, TIMEOUT_US, FLOOR_US);
  EXPECT_EQ(budget.record(0), ScanBudget::Outcome::Dropped);
  EXPECT_EQ(budget.record(FLOOR_US - 1), ScanBudget::Outcome::Dropped);
  EXPECT_EQ(budget.record(FLOOR_US), ScanBudget::Outcome::Done);  // exactly the floor is a scan
  EXPECT_EQ(budget.total(), 3u);
  EXPECT_EQ(budget.sinceClean(), 3u);  // dropped calls count: the call was made
  ScanBudget noFloor(CAP, TIMEOUT_US, 0);
  EXPECT_EQ(noFloor.record(0), ScanBudget::Outcome::Done);
}

TEST(ScanBudget, StartsClean) {
  const ScanBudget budget(CAP, TIMEOUT_US, FLOOR_US);
  EXPECT_FALSE(budget.cleanDue());
  EXPECT_EQ(budget.total(), 0u);
}

TEST(FramePeriodScan, NineScansTpOuterRateInner) {
  const uint8_t tps[] = {1, 10, 100};
  const uint8_t rates[] = {0x22, 0x11, 0x44};
  size_t i = 0;
  for (const uint8_t tp : tps) {
    for (const uint8_t rate : rates) {
      const ScanParams p = framePeriodScan(i);
      EXPECT_EQ(p.tp, tp) << "scan " << i;
      EXPECT_EQ(p.rate, rate) << "scan " << i;
      ++i;
    }
  }
  EXPECT_EQ(i, FRAME_PERIOD_SCANS);
}

}  // namespace

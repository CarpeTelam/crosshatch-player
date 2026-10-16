// The gray engine's slot clock (lib/GameDisplayLab/SlotScheduler.h): the display-lab epic's D5 period, the fixed
// phase with skipped (never queued) slots, the wrap-safe comparisons, and the window statistics.

#include <gtest/gtest.h>

#include "SlotScheduler.h"

namespace {

using GameDisplayLab::SlotScheduler;

TEST(SlotScheduler, FirstSlotIsDueAtStart) {
  SlotScheduler s;
  EXPECT_FALSE(s.due(0));  // not started
  s.start(1000);
  EXPECT_FALSE(s.due(999));
  EXPECT_TRUE(s.due(1000));
  EXPECT_TRUE(s.due(5000));
  s.stop();
  EXPECT_FALSE(s.due(5000));
}

TEST(SlotScheduler, PeriodIsBusyPlusMarginWithAFloor) {
  SlotScheduler s;
  s.start(0);
  s.slotDone(0, 30000, 30000);  // 35 ms < 100 ms
  EXPECT_EQ(s.periodUs(), SlotScheduler::MIN_PERIOD_US);
  EXPECT_EQ(s.nextUs(), 100000u);
  EXPECT_FALSE(s.due(99999));
  EXPECT_TRUE(s.due(100000));
  s.slotDone(100000, 100000 + 180000, 180000);  // 185 ms
  EXPECT_EQ(s.periodUs(), 185000u);
  EXPECT_EQ(s.nextUs(), 285000u);
  EXPECT_TRUE(s.due(285000));
  EXPECT_EQ(s.skipped(), 0u);
  s.slotDone(0, 0, 95000);  // exactly at the floor
  EXPECT_EQ(s.periodUs(), 100000u);
  s.slotDone(0, 0, 95001);
  EXPECT_EQ(s.periodUs(), 100001u);
}

TEST(SlotScheduler, LateSlotSkipsWholePeriodsAndNeverQueues) {
  SlotScheduler s;
  s.start(0);
  // A 40 ms slot that ended 350 ms after it began (the loop was held up): periods at 100, 200 and 300 ms are past.
  s.slotDone(0, 350000, 40000);
  EXPECT_EQ(s.skipped(), 2u);  // 100 and 200 ms; the 300 ms slot is still due now
  EXPECT_EQ(s.nextUs(), 300000u);
  EXPECT_TRUE(s.due(350000));
  s.slotDone(350000, 360000, 40000);
  EXPECT_EQ(s.nextUs(), 450000u);
  EXPECT_EQ(s.skipped(), 2u);
  // Exactly one period late: that period is skipped, the next one is due now.
  s.slotDone(450000, 650000, 40000);
  EXPECT_EQ(s.skipped(), 3u);
  EXPECT_EQ(s.nextUs(), 650000u);
  // The count is per window: a new window starts it from 0, as does start().
  s.resetWindow(650000);
  EXPECT_EQ(s.skipped(), 0u);
  s.slotDone(650000, 900000, 40000);
  EXPECT_EQ(s.skipped(), 1u);
  s.start(900000);
  EXPECT_EQ(s.skipped(), 0u);
}

TEST(SlotScheduler, ComparisonsSurviveTheClockWrapping) {
  SlotScheduler s;
  const uint32_t nearWrap = 0xFFFFFFFFu - 50000;
  s.start(nearWrap);
  EXPECT_TRUE(s.due(nearWrap));
  s.slotDone(nearWrap, nearWrap + 20000, 20000);
  EXPECT_EQ(s.nextUs(), nearWrap + 100000u);  // wrapped past zero
  EXPECT_FALSE(s.due(nearWrap + 99999u));
  EXPECT_FALSE(s.due(0xFFFFFFFFu));
  EXPECT_TRUE(s.due(nearWrap + 100000u));
  EXPECT_TRUE(s.due(nearWrap + 150000u));
  s.slotDone(nearWrap + 100000u, nearWrap + 420000u, 20000);  // late across the wrap
  EXPECT_EQ(s.skipped(), 2u);
  EXPECT_EQ(s.nextUs(), nearWrap + 400000u);
}

TEST(SlotScheduler, WindowStatsGiveScansPerSecondAndMsPerScan) {
  SlotScheduler s;
  s.start(0);
  EXPECT_EQ(s.scansPerSecondTenths(0), 0u);
  EXPECT_EQ(s.msPerScan(), 0u);
  for (int i = 0; i < 10; ++i) s.record(2, 2 * 45000);  // ten slots of two 45 ms scans
  EXPECT_EQ(s.windowScans(), 20u);
  EXPECT_EQ(s.scansPerSecondTenths(2000000), 100u);  // 20 scans in 2 s: 10.0 scans/s
  EXPECT_EQ(s.scansPerSecondTenths(3000000), 66u);   // 6.6
  EXPECT_EQ(s.msPerScan(), 45u);
  s.resetWindow(3000000);
  EXPECT_EQ(s.windowScans(), 0u);
  EXPECT_EQ(s.msPerScan(), 0u);
  s.record(1, 15500);
  EXPECT_EQ(s.windowUs(3500000), 500000u);
  EXPECT_EQ(s.scansPerSecondTenths(3500000), 20u);
  EXPECT_EQ(s.msPerScan(), 15u);
}

}  // namespace

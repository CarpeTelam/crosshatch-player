// The transition test's request word (lib/GameDisplayLab/SweepControl.h): the requests' coalescing, the sticky Stop
// that gates panel entry, the InPanel state, the covered heartbeat and its wrap, and the Stop/InPanel handshake
// across two threads.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "SweepControl.h"

namespace {

using GameDisplayLab::SweepControl;
using Gate = SweepControl::Gate;

constexpr uint32_t GAP_US = 1000000;

TEST(SweepControl, ResetAsksForAReplayRunning) {
  SweepControl c;
  c.requestStop();
  c.reset(500);
  EXPECT_TRUE(c.runRequested());
  EXPECT_FALSE(c.insidePanel());
  EXPECT_EQ(c.heartbeatAgeUs(500), 0u);
  const SweepControl::Requests r = c.take();
  EXPECT_FALSE(r.stop);
  EXPECT_TRUE(r.run);
  EXPECT_TRUE(r.replay);
  EXPECT_FALSE(r.clean);
}

TEST(SweepControl, TakeClearsOnlyReplayAndClean) {
  SweepControl c;
  c.reset(0);
  c.requestClean();
  c.requestRun(true);
  c.requestReplay();
  ASSERT_EQ(c.enterPanel(0, GAP_US), Gate::Enter);
  const SweepControl::Requests first = c.take();
  EXPECT_TRUE(first.run);
  EXPECT_TRUE(first.replay);
  EXPECT_TRUE(first.clean);
  const SweepControl::Requests second = c.take();
  EXPECT_TRUE(second.run);  // a state, kept
  EXPECT_FALSE(second.replay);
  EXPECT_FALSE(second.clean);
  EXPECT_TRUE(c.insidePanel());  // kept
  c.requestStop();
  EXPECT_TRUE(c.take().stop);
  EXPECT_TRUE(c.take().stop);  // sticky
}

TEST(SweepControl, TwoPausesBeforeTheTaskServesThemCancelOut) {
  SweepControl c;
  c.reset(0);
  c.take();
  c.requestRun(!c.runRequested());
  c.requestRun(!c.runRequested());
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.run);
  EXPECT_FALSE(r.replay);
  EXPECT_FALSE(r.clean);
}

TEST(SweepControl, RequestRunFalsePausesAndTrueResumes) {
  SweepControl c;
  c.reset(0);
  c.take();
  c.requestRun(false);
  EXPECT_FALSE(c.runRequested());
  EXPECT_FALSE(c.take().run);
  c.requestRun(true);
  EXPECT_TRUE(c.runRequested());
  EXPECT_TRUE(c.take().run);
}

TEST(SweepControl, ReplayThenCleanLeavesRunOff) {
  SweepControl c;
  c.reset(0);
  c.take();
  c.requestReplay();
  c.requestClean();
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.replay);
  EXPECT_TRUE(r.clean);
  EXPECT_FALSE(r.run);
}

TEST(SweepControl, CleanThenReplayLeavesRunOn) {
  SweepControl c;
  c.reset(0);
  c.take();
  c.requestClean();
  EXPECT_FALSE(c.runRequested());
  c.requestReplay();
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.replay);
  EXPECT_TRUE(r.clean);
  EXPECT_TRUE(r.run);
}

TEST(SweepControl, StopIsStickyAndGatesEntry) {
  SweepControl c;
  c.reset(0);
  c.requestStop();
  EXPECT_EQ(c.enterPanel(0, GAP_US), Gate::Stop);
  EXPECT_FALSE(c.insidePanel());
  c.requestRun(true);
  c.requestReplay();
  EXPECT_EQ(c.enterPanel(0, GAP_US), Gate::Stop);
  EXPECT_TRUE(c.take().stop);
}

TEST(SweepControl, EnterSetsAndLeaveClearsInPanel) {
  SweepControl c;
  c.reset(0);
  EXPECT_EQ(c.enterPanel(10, GAP_US), Gate::Enter);
  EXPECT_TRUE(c.insidePanel());
  c.leavePanel();
  EXPECT_FALSE(c.insidePanel());
  EXPECT_TRUE(c.runRequested());  // leaving touches nothing else
}

TEST(SweepControl, StopWhileInsideKeepsInPanelUntilLeave) {
  SweepControl c;
  c.reset(0);
  ASSERT_EQ(c.enterPanel(0, GAP_US), Gate::Enter);
  c.requestStop();
  EXPECT_TRUE(c.insidePanel());
  c.leavePanel();
  EXPECT_FALSE(c.insidePanel());
  EXPECT_EQ(c.enterPanel(0, GAP_US), Gate::Stop);
}

TEST(SweepControl, CoveredOnlyPastTheGap) {
  SweepControl c;
  c.reset(1000);
  EXPECT_EQ(c.enterPanel(1000 + GAP_US, GAP_US), Gate::Enter);
  c.leavePanel();
  EXPECT_EQ(c.enterPanel(1000 + GAP_US + 1, GAP_US), Gate::Covered);
  EXPECT_FALSE(c.insidePanel());
  c.heartbeat(1000 + GAP_US);
  EXPECT_EQ(c.enterPanel(1000 + GAP_US + 1, GAP_US), Gate::Enter);
  c.leavePanel();
  c.requestStop();
  EXPECT_EQ(c.enterPanel(1000 + 5 * GAP_US, GAP_US), Gate::Stop);  // Stop wins over Covered
}

TEST(SweepControl, HeartbeatAgeWrapsAndNeverGoesNegative) {
  SweepControl c;
  c.reset(UINT32_MAX - 99);
  EXPECT_EQ(c.heartbeatAgeUs(UINT32_MAX), 99u);
  EXPECT_EQ(c.heartbeatAgeUs(400), 500u);
  EXPECT_EQ(c.enterPanel(400, GAP_US), Gate::Enter);
  c.leavePanel();
  EXPECT_EQ(c.enterPanel(GAP_US - 100 + 1, GAP_US), Gate::Covered);
  // A clock read before the owner's last heartbeat is not an age.
  c.heartbeat(5000);
  EXPECT_EQ(c.heartbeatAgeUs(4000), 0u);
  EXPECT_EQ(c.enterPanel(4000, GAP_US), Gate::Enter);
}

TEST(SweepControl, ATaskLoopingThroughThePanelSeesStopAndEndsOutside) {
  SweepControl c;
  c.reset(0);
  std::atomic<uint32_t> entries{0};
  std::atomic<bool> ended{false};
  std::thread task([&] {
    while (c.enterPanel(0, GAP_US) == Gate::Enter) {
      entries.fetch_add(1);
      c.leavePanel();
    }
    ended.store(true);
  });
  while (entries.load() < 1000) std::this_thread::yield();
  c.requestStop();
  // Once the owner sees the task outside after its Stop, the task never enters again.
  while (c.insidePanel()) std::this_thread::yield();
  const uint32_t seen = entries.load();
  task.join();
  EXPECT_TRUE(ended.load());
  EXPECT_EQ(entries.load(), seen);
  EXPECT_FALSE(c.insidePanel());
}

}  // namespace

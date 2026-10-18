// The transition test's request word (lib/GameDisplayLab/SweepControl.h): the requests' coalescing, the sticky Stop
// that gates panel entry, the InPanel state, the covered heartbeat and its wrap, and the Stop/InPanel handshake
// across two threads.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
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

TEST(SweepControl, HoldRunClearsRunSoOnePressResumes) {
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  EXPECT_FALSE(c.runRequested());
  EXPECT_FALSE(c.take().run);
  c.requestRun(!c.runRequested());  // the screen's toggle: one press
  EXPECT_TRUE(c.runRequested());
  EXPECT_TRUE(c.take().run);
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

TEST(SweepControl, StopRequestedIsSeenAndClearedOnlyByReset) {
  SweepControl c;
  c.reset(0);
  EXPECT_FALSE(c.stopRequested());
  c.requestStop();
  EXPECT_TRUE(c.stopRequested());
  c.take();
  c.requestRun(true);
  EXPECT_TRUE(c.stopRequested());
  c.reset(0);
  EXPECT_FALSE(c.stopRequested());
}

TEST(SweepControl, PreviousThenConfirmCleansAndRuns) {
  SweepControl c;
  c.reset(0);
  c.take();
  c.requestClean();                 // Previous
  c.requestRun(!c.runRequested());  // Confirm
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.clean);
  EXPECT_TRUE(r.run);
  EXPECT_FALSE(r.replay);
}

TEST(SweepControl, ATaskLoopingThroughThePanelSeesStopAndEndsOutside) {
  using Clock = std::chrono::steady_clock;
  // Shared with the thread, so a thread left running by a failed round never touches freed state.
  struct Shared {
    SweepControl control;
    std::atomic<uint32_t> entries{0};
    std::atomic<bool> ended{false};
  };
  constexpr int ROUNDS = 200;
  for (int round = 0; round < ROUNDS; ++round) {
    const auto shared = std::make_shared<Shared>();
    SweepControl& c = shared->control;
    c.reset(0);
    std::thread task([shared] {
      while (shared->control.enterPanel(0, GAP_US) == Gate::Enter) {
        shared->entries.fetch_add(1);
        shared->control.leavePanel();
      }
      shared->ended.store(true);
    });
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    const auto waitFor = [&](auto&& done) {
      while (!done()) {
        if (Clock::now() > deadline) return false;
        std::this_thread::yield();
      }
      return true;
    };
    bool inTime = waitFor([&] { return shared->entries.load() >= 20; });
    c.requestStop();
    // Once the owner sees the task outside after its Stop, the task never enters again.
    inTime = inTime && waitFor([&] { return !c.insidePanel(); });
    const uint32_t seen = shared->entries.load();
    inTime = inTime && waitFor([&] { return shared->ended.load(); });
    if (!inTime) {
      task.detach();
      FAIL() << "round " << round << ": the task did not stop within 10 s";
    }
    task.join();
    ASSERT_EQ(shared->entries.load(), seen) << "round " << round;
    ASSERT_FALSE(c.insidePanel()) << "round " << round;
  }
}

// ---- the hold and the presses (story dlab-g3's review: one compare-exchange per press, the task ends a hold) -------

TEST(SweepControl, HoldSetsHeldAndClearsRunInOneStep) {
  SweepControl c;
  c.reset(0);
  (void)c.take();
  EXPECT_FALSE(c.held());
  c.holdRun();
  const uint32_t word = c.snapshot();
  EXPECT_NE(word & SweepControl::HELD, 0u);
  EXPECT_EQ(word & SweepControl::RUN, 0u);
}

TEST(SweepControl, APressWhileHeldIsTheNextPicture) {
  // The viewer's press while held: Held cleared, and the task's next take() cleans and paints the next picture,
  // running, with presses waiting for it.
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  EXPECT_EQ(c.press(true), SweepControl::Press::Next);
  EXPECT_FALSE(c.held());
  EXPECT_TRUE(c.replayPending());
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.replay);
  EXPECT_TRUE(r.run);
  EXPECT_TRUE(r.replaying);
  EXPECT_FALSE(r.stop);
  EXPECT_FALSE(r.clean);
}

TEST(SweepControl, APressWhileAReplayIsPendingIsIgnored) {
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  ASSERT_EQ(c.press(true), SweepControl::Press::Next);
  const uint32_t before = c.snapshot();
  EXPECT_EQ(c.press(true), SweepControl::Press::Ignored);
  EXPECT_EQ(c.press(false), SweepControl::Press::Ignored);
  EXPECT_EQ(c.snapshot(), before);
  // Once the task has served it, a press toggles again.
  (void)c.take();
  c.replayServed();
  EXPECT_FALSE(c.replayPending());
  EXPECT_EQ(c.press(true), SweepControl::Press::Toggled);
  EXPECT_FALSE(c.runRequested());
}

TEST(SweepControl, TheViewersFirstReplayMakesPressesWait) {
  SweepControl c;
  c.reset(0, true);
  EXPECT_TRUE(c.replayPending());
  EXPECT_EQ(c.press(true), SweepControl::Press::Ignored);
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.replay);
  EXPECT_TRUE(r.run);
  EXPECT_TRUE(r.replaying);
  c.replayServed();
  EXPECT_EQ(c.press(true), SweepControl::Press::Toggled);
  // The transition test's reset keeps presses live from the start.
  SweepControl t;
  t.reset(0);
  EXPECT_FALSE(t.replayPending());
  EXPECT_EQ(t.press(false), SweepControl::Press::Toggled);
}

TEST(SweepControl, APressWithoutAHoldToggles) {
  SweepControl c;
  c.reset(0);
  (void)c.take();
  EXPECT_EQ(c.press(true), SweepControl::Press::Toggled);
  EXPECT_FALSE(c.runRequested());
  EXPECT_FALSE(c.replayPending());
  EXPECT_EQ(c.press(true), SweepControl::Press::Toggled);
  EXPECT_TRUE(c.runRequested());
  // The transition test's press while held is a resume, not a next picture; a stop rules out a next picture.
  c.holdRun();
  EXPECT_EQ(c.press(false), SweepControl::Press::Toggled);
  EXPECT_TRUE(c.runRequested());
  SweepControl s;
  s.reset(0);
  (void)s.take();
  s.holdRun();
  s.requestStop();
  EXPECT_EQ(s.press(true), SweepControl::Press::Toggled);
  EXPECT_FALSE(s.replayPending());
}

TEST(SweepControl, AResumeThenAPauseBeforeTheTaskServesKeepsTheHold) {
  // The press does not end the hold; the task does, when it serves the resume. Two presses before that leave the run
  // held, Held set (and preventAutoSleep's bound on it), Run clear.
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  EXPECT_EQ(c.press(false), SweepControl::Press::Toggled);  // resume
  EXPECT_TRUE(c.held());
  EXPECT_EQ(c.press(false), SweepControl::Press::Toggled);  // pause
  EXPECT_TRUE(c.held());
  EXPECT_FALSE(c.take().run);
  // A resume the task serves: it releases the hold.
  EXPECT_EQ(c.press(false), SweepControl::Press::Toggled);
  EXPECT_TRUE(c.take().run);
  c.releaseHold();
  EXPECT_FALSE(c.held());
}

TEST(SweepControl, AHoldBetweenTheOwnersLookAndItsWriteCannotDropAPress) {
  // The old read-then-write toggle, requestRun(!runRequested()), read Run set, the hold cleared it, and the write
  // cleared it again: the press vanished into the hold.
  SweepControl old;
  old.reset(0);
  (void)old.take();
  const bool wasRunning = old.runRequested();
  old.holdRun();
  old.requestRun(!wasRunning);
  EXPECT_FALSE(old.runRequested());  // the press changed nothing
  // press() decides on the word as it changes it: after the hold, the same press resumes (the transition test) or
  // paints the next picture (the viewer); before it, it pauses, and the hold then holds.
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  EXPECT_EQ(c.press(false), SweepControl::Press::Toggled);
  EXPECT_TRUE(c.runRequested());
  SweepControl v;
  v.reset(0);
  (void)v.take();
  v.holdRun();
  EXPECT_EQ(v.press(true), SweepControl::Press::Next);
  EXPECT_TRUE(v.runRequested());
}

TEST(SweepControl, ACleanDoesNotTouchTheHoldTheTaskReleasesIt) {
  // requestClean (no caller today) clears Run and asks a clean; the task's clean path releases the hold.
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  c.requestClean();
  const SweepControl::Requests r = c.take();
  EXPECT_TRUE(r.clean);
  EXPECT_FALSE(r.run);
  EXPECT_TRUE(c.held());
  c.releaseHold();
  EXPECT_FALSE(c.held());
}

TEST(SweepControl, AResetClearsTheHoldAndAPendingReplay) {
  SweepControl c;
  c.reset(0);
  (void)c.take();
  c.holdRun();
  ASSERT_EQ(c.press(true), SweepControl::Press::Next);
  c.reset(0);
  EXPECT_FALSE(c.held());
  EXPECT_FALSE(c.replayPending());
}

}  // namespace

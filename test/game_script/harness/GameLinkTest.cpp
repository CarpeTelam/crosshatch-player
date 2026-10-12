#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "FakeRtos.h"
#include "GameClock.h"
#include "GameLink.h"
#include "NearbySessionSupport.h"
#include "activities/RenderLock.h"

// The GameLink task on its own, over a work that counts its steps (entry 8 of epic-play-nearby): the constants R5 and
// R7 fix, the cooperative stop and its bound, the stack figure handed to the work on exit, and the clock the suites run
// on. NearbySessionTest runs the same task under a real NearbySession.

namespace {

using nearby::waitFor;

// Stands in for NearbySession::Core as the GameLinkWork: counts its calls, and can hold a step until released (a task
// that will not stop). More permissive than the device: it takes no lock and calls no link.
class CountingWork final : public GameLinkWork {
 public:
  void linkStep() override {
    ++steps;
    const uint64_t now = fakertos::S().nowMs.load();
    const uint64_t previous = lastStepMs.exchange(now);
    if (previous != 0 && now - previous < minGapMs.load()) minGapMs = now - previous;
    if (hold.load()) {
      held = true;
      while (hold.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  void linkStopped(const uint32_t stackHighWaterBytes) override {
    ++stopped;
    highWater = stackHighWaterBytes;
    stepsAtStop = steps.load();
  }

  std::atomic<int> steps{0};
  std::atomic<uint64_t> lastStepMs{0};
  std::atomic<uint64_t> minGapMs{UINT64_MAX};  // the least fake time between two steps
  std::atomic<int> stopped{0};
  std::atomic<uint32_t> highWater{0};
  std::atomic<int> stepsAtStop{0};
  std::atomic<bool> hold{false};
  std::atomic<bool> held{false};
};

class GameLinkTest : public ::testing::Test {
 protected:
  void SetUp() override { fakertos::reset(); }
  void TearDown() override {
    work.hold = false;
    EXPECT_TRUE(fakertos::waitNoTasks());
  }
  CountingWork work;
};

TEST_F(GameLinkTest, ConstantsAreR5AndR7s) {
  EXPECT_EQ(GameLink::TASK_STACK_BYTES, 4096u);
  EXPECT_EQ(GameLink::TASK_PRIORITY, 1);
  EXPECT_EQ(GameLink::TASK_CORE, 1);
  EXPECT_EQ(GameLink::STEP_MS, 10u);
}

TEST_F(GameLinkTest, StartRunsStepsOnATaskOfTheRightStackAndStopJoinsIt) {
  GameLink link(work);
  EXPECT_FALSE(link.running());
  ASSERT_TRUE(link.start(1));
  EXPECT_EQ(fakertos::S().tasksCreated.load(), 1);
  ASSERT_TRUE(waitFor([&] { return work.steps.load() >= 3; }));
  EXPECT_TRUE(link.running());
  {
    std::lock_guard<std::mutex> lock(fakertos::S().m);
    ASSERT_EQ(fakertos::S().tasks.size(), 1u);
    EXPECT_EQ(fakertos::S().tasks[0]->stackBytes, GameLink::TASK_STACK_BYTES);
  }
  EXPECT_TRUE(link.stop());
  EXPECT_FALSE(link.running());
  EXPECT_EQ(work.stopped.load(), 1);
  const int stepsAfterStop = work.steps.load();
  EXPECT_EQ(work.stepsAtStop.load(), stepsAfterStop) << "no step ran after linkStopped";
  EXPECT_TRUE(fakertos::waitNoTasks());
}

TEST_F(GameLinkTest, TheStackHighWaterGoesToTheWorkOnExit) {
  GameLink link(work);
  ASSERT_TRUE(link.start(1));
  ASSERT_TRUE(link.stop());
  EXPECT_GT(work.highWater.load(), 0u);
  EXPECT_EQ(work.highWater.load(), GameLink::TASK_STACK_BYTES / 2) << "the harness reports a constant";
}

TEST_F(GameLinkTest, StopWithoutStartIsTrueAndStopIsRepeatable) {
  GameLink link(work);
  EXPECT_TRUE(link.stop());
  ASSERT_TRUE(link.start(1));
  EXPECT_TRUE(link.stop());
  EXPECT_TRUE(link.stop());
  EXPECT_EQ(work.stopped.load(), 1);
}

TEST_F(GameLinkTest, AFailedCreateIsFalseAndLeavesNoTask) {
  GameLink link(work);
  fakertos::S().failNextCreate = true;
  EXPECT_FALSE(link.start(1));
  EXPECT_FALSE(link.running());
  EXPECT_EQ(fakertos::S().tasksCreated.load(), 0);
  EXPECT_TRUE(link.stop());
  EXPECT_EQ(work.steps.load(), 0);
}

TEST_F(GameLinkTest, AStepThatNeverReturnsMakesStopFalseWithinItsBound) {
  GameLink link(work);
  work.hold = true;
  ASSERT_TRUE(link.start(1));
  ASSERT_TRUE(waitFor([&] { return work.held.load(); }));
  const auto begin = std::chrono::steady_clock::now();
  EXPECT_FALSE(link.stop(40));
  const auto tookMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
  EXPECT_LT(tookMs, 2000) << "stop(40) is bounded by its 20 polls, not by the task";
  EXPECT_TRUE(link.running());
  EXPECT_EQ(work.stopped.load(), 0);
  work.hold = false;
  EXPECT_TRUE(link.stop(2000)) << "once the step returns the task ends";
  EXPECT_EQ(work.stopped.load(), 1);
}

TEST_F(GameLinkTest, TheStepWaitIsTheConfiguredOne) {
  // Each step waits stepMs by vTaskDelay, which also moves the fake clock by what it waited: over 40 steps of 5 ms the
  // clock has moved at least 150 ms (the test's own advance only adds).
  GameLink link(work);
  const uint64_t before = fakertos::S().nowMs.load();
  ASSERT_TRUE(link.start(5));
  ASSERT_TRUE(waitFor([&] { return work.steps.load() >= 40; }));
  ASSERT_TRUE(link.stop());
  EXPECT_GE(fakertos::S().nowMs.load() - before, 39u * 5u);
  // Every gap between two steps is at least the step wait (other advances of the clock only add to a gap), so a task
  // that waited less than configured is red, however much the test's own advance moved the clock.
  EXPECT_GE(work.minGapMs.load(), 5u);
}

TEST(GameLinkTicksTest, ARoundedUpWaitOfAtLeastOneTickForAPositiveWait) {
  EXPECT_EQ(GameLink::ticksFor(0, 1), 0u) << "zero is a yield";
  EXPECT_EQ(GameLink::ticksFor(10, 1), 10u);
  EXPECT_EQ(GameLink::ticksFor(2, 10), 1u) << "2 ms at 100 Hz is one tick, not the zero pdMS_TO_TICKS rounds to";
  EXPECT_EQ(GameLink::ticksFor(5, 10), 1u);
  EXPECT_EQ(GameLink::ticksFor(11, 10), 2u) << "rounded up";
  EXPECT_EQ(GameLink::ticksFor(1, 1), 1u);
  EXPECT_EQ(GameLink::ticksFor(7, 0), 7u) << "a tick of 0 ms is read as 1";
}

TEST_F(GameLinkTest, AStoppedGameLinkStartsNoSecondTask) {
  GameLink link(work);
  ASSERT_TRUE(link.start(1));
  EXPECT_TRUE(link.start(1)) << "already running";
  EXPECT_EQ(fakertos::S().tasksCreated.load(), 1);
  ASSERT_TRUE(link.stop());
  EXPECT_FALSE(link.start(1)) << "a GameLink starts once; true would promise a task that does not exist";
  EXPECT_EQ(fakertos::S().tasksCreated.load(), 1);
}

TEST(GameLinkClockTest, FakeRtosClockAgreesWithGameClock) {
  // FakeRtosClock stands in for GameClock: both read the harness clock in milliseconds, so they agree at one instant
  // and both move by exactly what the clock moves by.
  fakertos::reset();
  const nearby::FakeRtosClock fake;
  const GameClock device;
  EXPECT_EQ(fake.nowMs(), device.nowMs());
  const uint64_t before = fake.nowMs();
  fakertos::advance(7);
  EXPECT_EQ(fake.nowMs() - before, 7u);
  EXPECT_EQ(device.nowMs() - before, 7u);
  EXPECT_GE(fake.nowMs(), before) << "monotonic";
}

TEST(GameLinkRenderLockTest, TheProbeCountsATaskThatTakesTheRenderLock) {
  // Without this the "no task takes the RenderLock" assertions of the nearby suites could pass because nothing counted.
  fakertos::reset();
  fakelock::reset();
  struct Taker {
    static void entry(void* flag) {
      {
        RenderLock lock;
        RenderLock tried(RenderLock::Mode::Try);
      }
      static_cast<std::atomic<bool>*>(flag)->store(true);
    }
  };
  std::atomic<bool> finished{false};
  TaskHandle_t handle = nullptr;
  ASSERT_EQ(xTaskCreatePinnedToCore(&Taker::entry, "taker", 4096, &finished, 1, &handle, 1), pdPASS);
  ASSERT_TRUE(waitFor([&] { return finished.load(); }));
  EXPECT_EQ(fakelock::taskAcquisitions.load(), 1);
  EXPECT_EQ(fakelock::taskAttempts.load(), 2) << "the Try that found the lock held by its own thread is an attempt too";
  // A thread that is no task is not counted.
  fakelock::reset();
  {
    RenderLock lock;
  }
  EXPECT_EQ(fakelock::taskAcquisitions.load(), 0);
  EXPECT_EQ(fakelock::taskAttempts.load(), 0);
  EXPECT_TRUE(fakertos::waitNoTasks());
}

}  // namespace

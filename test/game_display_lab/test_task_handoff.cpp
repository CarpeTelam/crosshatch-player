// The sweep task's end against its owner's give-up, and the stop bookkeeping (lib/GameDisplayLab/TaskHandoff.h,
// story dlab-g3): the exchange either side can lose, a cover seen while the task is stopping, and a Back that gave up
// followed by Home detaching at once.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "TaskHandoff.h"

namespace {

using GameDisplayLab::detachTask;
using GameDisplayLab::finishTask;
using GameDisplayLab::InertAnswers;
using GameDisplayLab::StopTracker;
using GameDisplayLab::TaskState;

struct Client {
  int id;
};

TEST(TaskHandoff, ADetachThatLosesTheExchangeUndoesItselfAndJoins) {
  Client screen{1};
  Client inert{2};
  std::atomic<TaskState> state{TaskState::Running};
  std::atomic<int> registry{0};
  std::atomic<Client*> client{&screen};
  ASSERT_TRUE(finishTask(state));  // the task ended first
  EXPECT_FALSE(detachTask(state, registry, client, &inert));
  EXPECT_EQ(registry.load(), 0);
  EXPECT_EQ(client.load(), &screen);
  EXPECT_EQ(state.load(), TaskState::Done);
}

TEST(TaskHandoff, ADetachThatWinsLeavesTheTaskToFreeItself) {
  Client screen{1};
  Client inert{2};
  std::atomic<TaskState> state{TaskState::Running};
  std::atomic<int> registry{0};
  std::atomic<Client*> client{&screen};
  ASSERT_TRUE(detachTask(state, registry, client, &inert));
  EXPECT_EQ(registry.load(), 1);
  EXPECT_EQ(client.load(), &inert);
  // The task's own end then finds it detached: it frees itself.
  EXPECT_FALSE(finishTask(state));
  EXPECT_EQ(state.load(), TaskState::Detached);
}

TEST(TaskHandoff, TheTaskAndItsOwnerRaceToOneEnd) {
  Client screen{1};
  Client inert{2};
  for (int round = 0; round < 2000; ++round) {
    std::atomic<TaskState> state{TaskState::Running};
    std::atomic<int> registry{0};
    std::atomic<Client*> client{&screen};
    std::atomic<bool> go{false};
    bool finished = false;
    bool detached = false;
    // What the task sees when it loses: the inert client and the registry already published (detachTask stores both
    // before its exchange; the task's acquire on losing sees them).
    Client* lostClient = nullptr;
    int lostRegistry = -1;
    std::thread task([&] {
      while (!go.load()) {
      }
      finished = finishTask(state);
      if (!finished) {
        lostClient = client.load(std::memory_order_relaxed);
        lostRegistry = registry.load(std::memory_order_relaxed);
      }
    });
    std::thread owner([&] {
      while (!go.load()) {
      }
      detached = detachTask(state, registry, client, &inert);
    });
    go.store(true);
    task.join();
    owner.join();
    ASSERT_NE(finished, detached) << round;
    EXPECT_EQ(registry.load(), detached ? 1 : 0) << round;
    EXPECT_EQ(client.load(), detached ? &inert : &screen) << round;
    if (!finished) {
      EXPECT_EQ(lostClient, &inert) << round;
      EXPECT_EQ(lostRegistry, 1) << round;
    }
  }
}

TEST(TaskHandoff, TheInertAnswersLeaveTheRunsEndToTheStop) {
  // A hook answered by the inert client between detachTask's publish and a lost exchange must not decide the run's end
  // the owner then joins and reads: no slot or cap clean ends the run (End::Finished), and a paint not made is the
  // stop's (paintFailed with a stop asked is End::Stopped).
  static_assert(!InertAnswers::slotFinishesRun, "a slot answered inert never finishes the run");
  static_assert(InertAnswers::capCleanGoesOn, "a cap clean answered inert never ends the run");
  static_assert(!InertAnswers::slotReapplies && !InertAnswers::keepHolding, "nothing is reapplied or held");
  static_assert(!InertAnswers::replayPainted && !InertAnswers::slotPrepared, "no paint is made: the stop ends it");
  static_assert(InertAnswers::clockStill, "the sweep clock stands still");
  SUCCEED();
}

TEST(TaskHandoff, ACoverSeenWhileStoppingWaitsForTheJoin) {
  StopTracker stops;
  stops.reset();
  stops.ask(100);
  EXPECT_FALSE(stops.coverSeen(false));  // still stopping: kept
  EXPECT_TRUE(stops.joined());           // applied at the join
  EXPECT_FALSE(stops.joined());          // once
  EXPECT_TRUE(stops.coverSeen(true));    // joined already: applied now
  EXPECT_FALSE(stops.joined());
  // A new run forgets a pending cover.
  stops.coverSeen(false);
  stops.reset();
  EXPECT_FALSE(stops.joined());
}

TEST(TaskHandoff, BackGivesUpThenHomeDetachesAtOnce) {
  constexpr uint32_t WAIT_MS = 20000;
  StopTracker stops;
  stops.reset();
  EXPECT_FALSE(stops.expired(50000, WAIT_MS));  // no stop asked: no deadline
  stops.ask(1000);                              // Back
  EXPECT_FALSE(stops.expired(20999, WAIT_MS));
  EXPECT_TRUE(stops.expired(21000, WAIT_MS));  // Back gives up
  EXPECT_TRUE(stops.giveUp());                 // logged once
  EXPECT_FALSE(stops.giveUp());
  // A join forgets the give-up: a later stop that gives up logs again.
  stops.joined();
  EXPECT_TRUE(stops.giveUp());
  EXPECT_FALSE(stops.giveUp());
  stops.ask(25000);                            // Home later: the same deadline, already passed
  EXPECT_TRUE(stops.expired(25000, WAIT_MS));  // endLocked detaches at once
  // Home first: its stop and the exit after it share one 20 s.
  stops.reset();
  stops.ask(0);
  stops.ask(19000);
  EXPECT_TRUE(stops.expired(20000, WAIT_MS));
  // Across the millisecond clock's wrap.
  stops.reset();
  stops.ask(UINT32_MAX - 100);
  EXPECT_FALSE(stops.expired(100, WAIT_MS));
  EXPECT_TRUE(stops.expired(WAIT_MS, WAIT_MS));
}

}  // namespace

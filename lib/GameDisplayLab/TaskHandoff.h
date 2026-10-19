#pragma once

#include <atomic>
#include <cstdint>

// The sweep task's end against its owner's give-up, and the owner's stop bookkeeping (GrayRunner, story dlab-g3),
// pure so their races are pinned by test_task_handoff.cpp: the runner holds a TaskState word, the reap registry and its
// client pointer, and a StopTracker on the loop task.
namespace GameDisplayLab {

// The task's end, decided once: Running becomes Done (the task's own end) or Detached (its owner gave it up), never
// both.
enum class TaskState : uint8_t { Running, Done, Detached };

// The task's side, as its run returns: true when it ended first (Done: it parks, and its owner deletes it), false when
// its owner detached it first (the task frees what it was given and deletes itself).
inline bool finishTask(std::atomic<TaskState>& state) {
  TaskState expected = TaskState::Running;
  return state.compare_exchange_strong(expected, TaskState::Done, std::memory_order_acq_rel);
}

// What the inert client (GrayRunner.cpp's DetachedClient) answers each hook. A detach always follows a stop, and
// detachTask publishes the inert client before the exchange that decides, so a hook the task calls in that window gets
// these answers even when the task then ends first and its owner joins it and reads its end. They change nothing
// about that end, which is the stop's (End::Stopped): a slot never finishes the run, a cap clean never ends it, and a
// paint that is not made (a replay, a slot) ends it as the stop (GrayRunner::paintFailed reads a stop asked as
// Stopped); the sweep clock stands still and no hold is kept.
struct InertAnswers {
  static constexpr bool replayPainted = false;    // onReplay
  static constexpr bool keepHolding = false;      // keepHolding
  static constexpr bool capCleanGoesOn = true;    // onCapClean (false would be End::Finished)
  static constexpr bool slotPrepared = false;     // prepareSlot
  static constexpr bool slotReapplies = false;    // afterSlot's reapply
  static constexpr bool slotFinishesRun = false;  // afterSlot's finished (true would be End::Finished)
  static constexpr bool clockStill = true;        // clockStill
};

// The owner's side, past the stop's deadline: `client` set to `inert` and `registry` counted before the one exchange
// that decides, so a hook the task calls from then on is the inert one's and the registry never misses a detached
// task. True: detached, and the owner touches nothing the task was given from here (the task may free it at once).
// False: the task ended first; both are undone and the owner joins it (a hook in between had InertAnswers).
template <typename Client>
bool detachTask(std::atomic<TaskState>& state, std::atomic<int>& registry, std::atomic<Client*>& client,
                Client* const inert) {
  Client* const own = client.load();
  client.store(inert);
  registry.fetch_add(1);
  TaskState expected = TaskState::Running;
  if (state.compare_exchange_strong(expected, TaskState::Detached, std::memory_order_acq_rel)) return true;
  registry.fetch_sub(1);
  client.store(own);
  return false;
}

// The loop task's stop bookkeeping for one run: the deadline every stop shares (from the first asked: Home's stop and
// the exit after it wait out one deadline together), stop()'s give-up (logged once), and a cover the view saw while
// the task was still stopping (the run's end is the task's until it is joined, so the cover waits for the join).
class StopTracker {
 public:
  // A run's start.
  void reset() {
    asked_ = false;
    gaveUp_ = false;
    coverPending_ = false;
  }
  // A stop asked at `nowMs`: the first starts the deadline.
  void ask(const uint32_t nowMs) {
    if (asked_) return;
    asked_ = true;
    askedMs_ = nowMs;
  }
  // The deadline, `waitMs` after the first stop asked, has passed (a stop that gave up leaves none: the next stop
  // detaches or gives up at once). Wraps with the millisecond clock.
  bool expired(const uint32_t nowMs, const uint32_t waitMs) const { return asked_ && nowMs - askedMs_ >= waitMs; }
  // stop() gave up waiting: true the first time, to log it once.
  bool giveUp() {
    const bool first = !gaveUp_;
    gaveUp_ = true;
    return first;
  }
  // The view saw a cover: true to apply it now (the task was joined), false when it is kept for the join.
  bool coverSeen(const bool joined) {
    if (joined) return true;
    coverPending_ = true;
    return false;
  }
  // The task was joined: true when a cover seen while it was stopping is to be applied now.
  bool joined() {
    const bool cover = coverPending_;
    coverPending_ = false;
    gaveUp_ = false;
    return cover;
  }

 private:
  bool asked_ = false;
  bool gaveUp_ = false;
  bool coverPending_ = false;
  uint32_t askedMs_ = 0;
};

}  // namespace GameDisplayLab

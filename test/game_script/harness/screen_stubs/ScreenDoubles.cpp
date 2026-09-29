// The doubles of the screen harness that need a definition: the ActivityManager singleton,
// RenderLock, and the global `gpio`. See the headers next to this file.

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "ActivityManager.h"
#include "HalGPIO.h"
#include "RenderLockProbe.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"

HalGPIO gpio;
ActivityManager activityManager;

namespace {

std::mutex renderMutex;
std::atomic<bool> lockHeld{false};
std::atomic<std::thread::id> lockOwner{};

// Takes the lock for the calling thread. False when it did not: a second lock by its own
// holder (counted, never waited on), or a Try that found it taken.
bool acquire(const bool tryOnly) {
  if (lockHeld.load() && lockOwner.load() == std::this_thread::get_id()) {
    // A Try by the holder just fails, as on the device; only a blocking lock would hang.
    if (!tryOnly) ++fakelock::selfDeadlocks;
    return false;
  }
  if (tryOnly) {
    if (!renderMutex.try_lock()) return false;
  } else {
    renderMutex.lock();
  }
  lockOwner = std::this_thread::get_id();
  lockHeld = true;
  ++fakelock::acquisitions;
  return true;
}

}  // namespace

bool fakelock::held() { return lockHeld.load(); }

RenderLock::RenderLock(const Mode mode) { isLocked = acquire(mode == Mode::Try); }
RenderLock::RenderLock(Activity&) { isLocked = acquire(false); }
RenderLock::~RenderLock() { unlock(); }

void RenderLock::unlock() {
  if (!isLocked) return;
  isLocked = false;
  lockHeld = false;
  lockOwner = std::thread::id{};
  renderMutex.unlock();
}

bool RenderLock::peek() { return lockHeld.load(); }

void ActivityManager::requestUpdate(const bool immediate) {
  if (immediate) {
    ++asks.immediateUpdates;
  } else {
    ++asks.updates;
  }
}
void ActivityManager::requestUpdateAndWait() { ++asks.immediateUpdates; }
#if FREEINK_CAP_GAMES
void ActivityManager::goToGames() {
  ++asks.goToGames;
  if (onGoToGames) onGoToGames();
}
#endif
void ActivityManager::goHome(HomeMenuItem, bool) { ++asks.goHome; }
void ActivityManager::goToReader(std::string, bool) { ++asks.goToReader; }
void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  ++asks.pushed;
  pushedActivities.push_back(std::move(activity));
}
void ActivityManager::popActivity() { ++asks.popped; }

void ActivityManager::exitHolding(Activity& activity) {
  RenderLock lock;
  activity.onExit();
}

void ActivityManager::reset() {
  asks = Asks{};
  pushedActivities.clear();
  onGoToGames = nullptr;
  seenUpdates = 0;
  fakelock::reset();
}

// Activity's members, as src/activities/Activity.cpp has them.
void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }
void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }
void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }
void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }
void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }
void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }
void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler handler) {
  this->resultHandler = std::move(handler);
  activityManager.pushActivity(std::move(activity));
}
void Activity::setResult(ActivityResult&& value) { this->result = std::move(value); }
void Activity::finish() { activityManager.popActivity(); }

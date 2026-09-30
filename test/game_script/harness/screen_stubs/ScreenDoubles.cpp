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

ActivityManager::ActivityManager() = default;
ActivityManager::~ActivityManager() = default;

void ActivityManager::requestUpdate(const bool immediate) {
  calls.push_back("requestUpdate");
  if (immediate) {
    ++asks.immediateUpdates;
  } else {
    ++asks.updates;
  }
}
void ActivityManager::requestUpdateAndWait() {
  calls.push_back("requestUpdateAndWait");
  ++asks.immediateUpdates;
}
void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  calls.push_back("replaceActivity");
  ++asks.replaced;
  replacements.push_back(std::move(newActivity));
}
void ActivityManager::goToFileTransfer() {
  calls.push_back("goToFileTransfer");
  ++asks.goToFileTransfer;
}
void ActivityManager::goToUsbDrive() {
  calls.push_back("goToUsbDrive");
  ++asks.goToUsbDrive;
}
void ActivityManager::goToSettings() {
  calls.push_back("goToSettings");
  ++asks.goToSettings;
}
void ActivityManager::goToFileBrowser(std::string path) {
  calls.push_back("goToFileBrowser");
  ++asks.goToFileBrowser;
  lastPath = std::move(path);
}
void ActivityManager::goToLibrary() {
  calls.push_back("goToLibrary");
  ++asks.goToLibrary;
}
void ActivityManager::goToBrowser() {
  calls.push_back("goToBrowser");
  ++asks.goToBrowser;
}
void ActivityManager::goToReader(std::string path, bool) {
  calls.push_back("goToReader");
  ++asks.goToReader;
  lastPath = std::move(path);
}
void ActivityManager::goToSleep(bool) {
  calls.push_back("goToSleep");
  ++asks.goToSleep;
}
void ActivityManager::goToBoot() {
  calls.push_back("goToBoot");
  ++asks.goToBoot;
}
void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style) {
  calls.push_back("goToFullScreenMessage");
  ++asks.goToFullScreenMessage;
  lastMessage = std::move(message);
}
void ActivityManager::goToCrashReport() {
  calls.push_back("goToCrashReport");
  ++asks.goToCrashReport;
}
#if FREEINK_CAP_GAMES
void ActivityManager::goToGames() {
  calls.push_back("goToGames");
  ++asks.goToGames;
  if (onGoToGames) onGoToGames();
}
#endif
void ActivityManager::goHome(HomeMenuItem item, bool cleanInitialRefresh) {
  calls.push_back("goHome");
  ++asks.goHome;
  lastHomeItem = item;
  lastCleanInitialRefresh = cleanInitialRefresh;
}
void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  calls.push_back("pushActivity");
  ++asks.pushed;
  pushedActivities.push_back(std::move(activity));
}
void ActivityManager::popActivity() {
  calls.push_back("popActivity");
  ++asks.popped;
}

void ActivityManager::exitHolding(Activity& activity) {
  RenderLock lock;
  activity.onExit();
}

void ActivityManager::reset() {
  asks = Asks{};
  calls.clear();
  lastPath.clear();
  lastMessage.clear();
  lastHomeItem = HomeMenuItem::NONE;
  lastCleanInitialRefresh = false;
  replacements.clear();
  pushedActivities.clear();
  onGoToGames = nullptr;
  seenUpdates = 0;
  fakelock::reset();
}

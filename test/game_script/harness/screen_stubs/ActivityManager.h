#pragma once

// The ActivityManager a screen under test talks to: every public member of the real
// src/activities/ActivityManager.h that a screen calls (the navigation calls, the update
// requests, the stack calls, the queries), recorded, with the manager's lock discipline modelled
// (below). None of the real manager's stack, render task, or pending-activity queue: a test
// drives an activity's onEnter, loop, render, and onExit itself, as the real manager would, and
// reads what the activity asked for. The calls are counted in `asks`, named in order in `calls`,
// and their arguments kept (`lastPath`, `lastMessage`, `lastHomeItem`); an activity handed to
// replaceActivity, pushActivity, or startActivityForResult is kept in `replacements` /
// `pushedActivities`, so a test can run it. The hook `onGoToGames` runs inside goToGames().
//
// The surface is declared in full, because the entries that build screens on it (5, 8 to 12) may not
// edit this file: a call added to the real manager later is not here until this file is extended,
// and the compile of a screen that calls it says so.
//
// The lock model (RenderLock, in ScreenDoubles.cpp, is a non-recursive mutex that counts a
// second blocking lock by the same thread instead of hanging): exitHolding() calls onExit() with
// the lock held, as ActivityManager::exitActivity does, so an activity that takes RenderLock in
// onExit (or a destructor, destroyHolding) is caught as `selfDeadlocks` (12cc816). The harness
// renders on the test thread between loop passes, not beside them, so a race between the render
// task and the loop task is never staged here (the review's note on ## 3.7).

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/RenderLock.h"
#include "util/ScreenshotInfo.h"

class Activity;

enum class HomeMenuItem {
  NONE,
  FILE_BROWSER,
  LIBRARY,
  OPDS_BROWSER,
  FILE_TRANSFER,
  SETTINGS_MENU,
#if FREEINK_CAP_GAMES
  GAMES,
#endif
};

class ActivityManager {
 public:
  ActivityManager();
  ~ActivityManager();

  // What the activity asked of the manager since reset().
  struct Asks {
    int updates = 0;           // requestUpdate(false)
    int immediateUpdates = 0;  // requestUpdate(true) and requestUpdateAndWait
    int replaced = 0;          // replaceActivity
    int goToFileTransfer = 0;
    int goToUsbDrive = 0;
    int goToSettings = 0;
    int goToFileBrowser = 0;
    int goToLibrary = 0;
    int goToBrowser = 0;
    int goToReader = 0;
    int goToSleep = 0;
    int goToBoot = 0;
    int goToFullScreenMessage = 0;
    int goToCrashReport = 0;
    int goToGames = 0;
    int goHome = 0;
    int pushed = 0;
    int popped = 0;
  };

  // The real manager's public members, recorded.
  void begin() {}
  void loop() {}
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);
  void goToFileTransfer();
  void goToUsbDrive();
  void goToSettings();
  void goToFileBrowser(std::string path = {});
  void goToLibrary();
  void goToBrowser();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void goToSleep(bool fromTimeout = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
#if FREEINK_CAP_GAMES
  void goToGames();
#endif
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);
  void pushActivity(std::unique_ptr<Activity>&& activity);
  void popActivity();

  bool preventAutoSleep() const { return false; }
  bool requiresExclusiveStorageLoop() const { return false; }
  bool isReaderActivity() const { return false; }
  bool handleForcedRefresh() { return false; }
  bool skipLoopDelay() const { return false; }
  ScreenshotInfo getScreenshotInfo() const { return {}; }

  void requestUpdate(bool immediate = false);
  void requestUpdateAndWait();

  // The manager's side of an activity's exit and destruction, RenderLock held.
  void exitHolding(Activity& activity);
  template <typename T>
  void destroyHolding(std::unique_ptr<T>& activity) {
    RenderLock lock;
    activity.reset();
  }

  // The updates a loop pass would render: a test's render call follows one.
  bool updateRequested() const { return asks.updates + asks.immediateUpdates > seenUpdates; }
  void markRendered() { seenUpdates = asks.updates + asks.immediateUpdates; }

  void reset();

  Asks asks;
  std::vector<std::string> calls;  // the names of the calls, in order ("goHome", "replaceActivity", ...)
  std::string lastPath;            // goToFileBrowser's and goToReader's
  std::string lastMessage;         // goToFullScreenMessage's
  HomeMenuItem lastHomeItem = HomeMenuItem::NONE;
  bool lastCleanInitialRefresh = false;
  std::vector<std::unique_ptr<Activity>> replacements;      // what replaceActivity was given, in order
  std::vector<std::unique_ptr<Activity>> pushedActivities;  // what pushActivity was given
  // Called inside goToGames(), so a test can read what was true at the moment of the ask (a VM
  // stopped, a store written) as well as after it.
  std::function<void()> onGoToGames;

 private:
  int seenUpdates = 0;
};

extern ActivityManager activityManager;

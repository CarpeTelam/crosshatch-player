#pragma once

// The ActivityManager a screen under test talks to: what Activity.cpp and the games
// screens call, recorded, with the manager's lock discipline modelled (below). None of the
// real manager's stack, render task, or pending-activity queue: a test drives an activity's
// onEnter, loop, render, and onExit itself, as the real manager would, and reads what the
// activity asked for. Entry 5 (the Games list) records its own asks here through
// `onGoToGames`-style hooks it adds in its own double; this file only counts.
//
// The lock model (RenderLock, in ScreenDoubles.cpp, is a non-recursive mutex that counts a
// second lock by the same thread instead of hanging): exitHolding() calls onExit() with the
// lock held, as ActivityManager::exitActivity does, so an activity that takes RenderLock in
// onExit (or a destructor, destroyHolding) is caught as `selfDeadlocks` (12cc816).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/RenderLock.h"

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
  // What the activity asked of the manager since reset().
  struct Asks {
    int updates = 0;           // requestUpdate(false)
    int immediateUpdates = 0;  // requestUpdate(true) and requestUpdateAndWait
    int goToGames = 0;
    int goHome = 0;
    int goToReader = 0;
    int pushed = 0;
    int popped = 0;
  };

  void requestUpdate(bool immediate = false);
  void requestUpdateAndWait();
#if FREEINK_CAP_GAMES
  void goToGames();
#endif
  void goHome(HomeMenuItem item = HomeMenuItem::NONE, bool cleanInitialRefresh = false);
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void pushActivity(std::unique_ptr<Activity>&& activity);
  void popActivity();

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
  std::vector<std::unique_ptr<Activity>> pushedActivities;
  // Called inside goToGames(), so a test can read what was true at the moment of the ask (a VM
  // stopped, a store written) as well as after it.
  std::function<void()> onGoToGames;

 private:
  int seenUpdates = 0;
};

extern ActivityManager activityManager;

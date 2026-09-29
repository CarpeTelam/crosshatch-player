#pragma once

// activities/Activity.h with the same class surface, over the doubles of this folder. The real
// header includes src/activities/ActivityManager.h by a quoted path from its own folder, which
// no include order can shadow, so the activity base is a copy: the members and virtuals the
// games screens override or call, in the real order, and the real ActivityResult.h. Its
// member functions are in ScreenDoubles.cpp, one line each as in the real Activity.cpp. A
// virtual added to the real class is not here until a screen needs it.

#include <Logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/RenderLock.h"
#include "util/ScreenshotInfo.h"

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  virtual void requestUpdate(bool immediate = false);
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  virtual bool isReaderActivity() const { return false; }
  virtual bool handleForcedRefresh() { return false; }
  virtual bool isHomeActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);
  void setResult(ActivityResult&& result);
  static void finish();
  static void onGoHome(HomeMenuItem item = HomeMenuItem::NONE);
  static void onSelectBook(const std::string& path);
};

#pragma once

// The input a screen reads, scripted by the test: the real MappedInputManager's calls with
// the names, signatures, and one-frame meaning the screens use (each is true for the frame
// the test set it, and clear() ends the frame). No orientation, home key, or gesture
// classification: the screens under test never reach them (a swipe is read through `gpio`).
// Not modelled: wasSwipe, wasBackGesture, wasHomeGesture, the light panel gesture,
// rowTouch, and wasTapInRect (AGENTS.md forbids rowTouch and wasTapInRect in new screens).

#include <HalGPIO.h>

#include <set>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer&) : gpio(gpio) {}

  bool wasPressed(const Button button) const { return pressed.count(button) != 0; }
  bool wasReleased(const Button button) const { return released.count(button) != 0; }
  bool isPressed(const Button button) const { return pressed.count(button) != 0; }
  bool hasTouch() const { return true; }

  bool wasScreenTapped(int& x, int& y) const { return touch.tapped ? at(x, y) : false; }
  bool wasScreenTouchDown(int& x, int& y) const { return touch.down ? at(x, y) : false; }
  bool wasScreenLongPress(int& x, int& y) const { return touch.longPress ? at(x, y) : false; }
  bool isScreenTouchHeld(int& x, int& y) const { return touch.held ? at(x, y) : false; }
  bool wasScreenTouchReleased() const { return touch.released; }

  // The label the button at each of the four front positions shows (no remapping here).
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const {
    return {back, confirm, previous, next};
  }

  // ---- what a test scripts, for the frame the next loop() call reads ----
  struct Touch {
    bool tapped = false;     // wasScreenTapped
    bool down = false;       // wasScreenTouchDown
    bool longPress = false;  // wasScreenLongPress
    bool held = false;       // isScreenTouchHeld
    bool released = false;   // wasScreenTouchReleased
    int x = 0;
    int y = 0;
  };

  void press(const Button button) { pressed.insert(button); }
  // A button released this frame (what a click ends in); also counts as pressed on the way.
  void click(const Button button) {
    pressed.insert(button);
    released.insert(button);
  }
  // A tap at (x, y): the down edge, then the release with its position.
  void tap(const int x, const int y) {
    touch.tapped = touch.released = touch.down = true;
    touch.x = x;
    touch.y = y;
  }
  void longPress(const int x, const int y) {
    touch.longPress = true;
    touch.x = x;
    touch.y = y;
  }
  // A swipe from (x0, y0) to (x1, y1), read from `gpio` as the match reads it.
  void swipe(const float x0, const float y0, const float x1, const float y1) {
    gpio.swipe = {true, x0, y0, x1, y1};
    touch.released = true;  // a swipe ends in a raw release the tap classifier never reports
    touch.x = -1;
    touch.y = -1;
  }
  // The frame is over: nothing is pressed or touched any more.
  void clear() {
    pressed.clear();
    released.clear();
    touch = Touch{};
    gpio.swipe = HalGPIO::Swipe{};
  }

  std::set<Button> pressed;
  std::set<Button> released;
  Touch touch;

 private:
  bool at(int& x, int& y) const {
    x = touch.x;
    y = touch.y;
    return true;
  }

  HalGPIO& gpio;
};

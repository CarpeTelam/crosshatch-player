#pragma once

// The input a screen reads, scripted by the test: every public member of the real
// MappedInputManager (src/MappedInputManager.h) a screen calls, with the names, signatures, and
// one-frame meaning the screens use (each is true for the frame the test set it, and clear()
// ends the frame). No orientation, home key, or gesture classification of its own: a swipe is a
// direction the test names (`swipeDir`, for wasSwipe) or a point pair on `gpio` (for the games
// canvas); the edge gestures (Back, Home, menu, light panel) are the real class's raw
// classification of a swipe, which the test scripts as the boolean it wants. mapLabels and
// mapDirectionalLabels return their arguments in the real front-button order, with no remapping:
// what the real mapper does to a label (AGENTS.md: users remap the front buttons) is not
// exercised. isPressed is wasPressed (no held-versus-edge distinction).
//
// Held buttons (`hold`): getHeldTime() is what a test set with `hold(button, ms)` (one time for every button, as the
// real manager's is for the button held: a test holds one at a time), and wasLongPressed(button,
// thresholdMs) is the real one's rule: true once per hold, when the button is pressed and has been held for at least
// thresholdMs, after which the button's release is suppressed (consumeSuppressedRelease, which ActivityManager::loop
// calls and a test that drives an activity's loop() calls itself). `holdLong` is the older shortcut: a hold that
// crosses any threshold, with no suppression.

#include <HalGPIO.h>

#include <climits>
#include <cstdint>
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
  enum class SwipeDir { None, Left, Right, Up, Down };
  enum class RowTouch : uint8_t { None, Down, Tap };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(&renderer) {}

  void update(bool = false) const {}
  bool wasPressed(const Button button) const { return pressed.count(button) != 0; }
  bool wasReleased(const Button button) const { return released.count(button) != 0; }
  bool wasLongPressed(const Button button, const unsigned long thresholdMs) const {
    if (longPressed.count(button) != 0) return true;
    if (pressed.count(button) == 0) {
      firedLongPress.erase(button);  // let go: the next hold can fire again
      return false;
    }
    if (firedLongPress.count(button) != 0 || heldMs < thresholdMs) return false;
    firedLongPress.insert(button);
    suppressedRelease.insert(button);
    return true;
  }
  // True when a button whose long press fired has been released: the release is not for the screen.
  bool consumeSuppressedRelease() const {
    bool consumed = false;
    for (auto it = suppressedRelease.begin(); it != suppressedRelease.end();) {
      if (released.count(*it) != 0) {
        it = suppressedRelease.erase(it);
        consumed = true;
      } else {
        ++it;
      }
    }
    return consumed;
  }
  bool isPressed(const Button button) const { return pressed.count(button) != 0; }
  bool hasTouch() const { return true; }

  bool wasScreenTapped(int& x, int& y) const { return touch.tapped ? at(x, y) : false; }
  // The device's is a level, not an edge: true on every update while a still finger has been down 90 ms or more
  // (MappedInputManager.cpp's TOUCH_DOWN_SELECT_DELAY_MS over InputManager::isTouchTapCandidate). `touch.down` scripts
  // it for one frame; holdTouch() keeps it reported on every frame, across clear(), until liftTouch().
  bool wasScreenTouchDown(int& x, int& y) const {
    if (contact.held) {
      x = contact.x;
      y = contact.y;
      return true;
    }
    return touch.down ? at(x, y) : false;
  }
  bool wasScreenLongPress(int& x, int& y) const { return touch.longPress ? at(x, y) : false; }
  bool isScreenTouchHeld(int& x, int& y) const { return touch.held ? at(x, y) : false; }
  bool wasScreenTouchReleased() const { return touch.released; }
  // A tap released inside the rectangle.
  bool wasTapInRect(const int x, const int y, const int width, const int height) const {
    return touch.tapped && touch.x >= x && touch.x < x + width && touch.y >= y && touch.y < y + height;
  }

  // As the real header documents: a band of `rowCount` rows `rowStep` apart from `top`, hit only
  // within [xStart, xEnd) and, when `rowHeight` is not 0, the top `rowHeight` px of each step.
  // Down: a held contact, or the down edge, is on a row; Tap: a tap released on one.
  RowTouch rowTouch(int& row, const int top, const int rowStep, const int rowCount, const int xStart = 0,
                    const int xEnd = INT32_MAX, const int rowHeight = 0) const {
    if (!touch.tapped && !touch.down && !touch.held) return RowTouch::None;
    if (touch.x < xStart || touch.x >= xEnd || touch.y < top || rowStep <= 0) return RowTouch::None;
    const int index = (touch.y - top) / rowStep;
    if (index >= rowCount) return RowTouch::None;
    if (rowHeight > 0 && (touch.y - top) % rowStep >= rowHeight) return RowTouch::None;
    row = index;
    return touch.tapped ? RowTouch::Tap : RowTouch::Down;
  }
  // The horizontal variant for side-by-side buttons.
  RowTouch colTouch(int& col, const int left, const int colStep, const int colCount, const int yStart, const int yEnd,
                    const int colWidth = 0) const {
    if (!touch.tapped && !touch.down && !touch.held) return RowTouch::None;
    if (touch.y < yStart || touch.y >= yEnd || touch.x < left || colStep <= 0) return RowTouch::None;
    const int index = (touch.x - left) / colStep;
    if (index >= colCount) return RowTouch::None;
    if (colWidth > 0 && (touch.x - left) % colStep >= colWidth) return RowTouch::None;
    col = index;
    return touch.tapped ? RowTouch::Tap : RowTouch::Down;
  }

  SwipeDir wasSwipe() const { return swipeDir; }
  bool wasBackGesture() const { return backGesture; }
  bool wasHomeGesture() const { return homeGesture; }
  bool wasMenuGesture() const { return menuGesture; }
  bool wasReaderMenuSwipeUp() const { return false; }
  bool wasLightPanelGesture() const { return false; }
  bool wasAnyPressed() const { return !pressed.empty(); }
  bool wasAnyReleased() const { return !released.empty(); }
  unsigned long getHeldTime() const { return heldMs; }
  const GfxRenderer& getRenderer() const { return *renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const {
    return {back, confirm, previous, next};
  }
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const {
    (void)left, (void)right, (void)up, (void)down;
    return {back, confirm, left, right};
  }
  int getPressedFrontButton() const { return -1; }
  [[nodiscard]] bool isNavDirectionSwapped() const { return false; }

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
  void holdLong(const Button button) { longPressed.insert(button); }
  // A button held down for `ms` so far, this frame: pressed (isPressed), and getHeldTime() is `ms`.
  void hold(const Button button, const unsigned long ms) {
    pressed.insert(button);
    heldMs = ms;
  }
  // The button is let go: its release edge this frame, and it is no longer held.
  void release(const Button button) {
    released.insert(button);
    firedLongPress.erase(button);
  }
  // A tap at (x, y): the down edge, then the release with its position.
  void tap(const int x, const int y) {
    touch.tapped = touch.released = touch.down = true;
    touch.x = x;
    touch.y = y;
  }
  // A finger held still at (x, y) from now on, reported by wasScreenTouchDown on every frame (the device's level) until
  // liftTouch(), which releases it there as a tap this frame.
  void holdTouch(const int x, const int y) { contact = {true, x, y}; }
  void liftTouch() {
    touch.tapped = touch.released = true;
    touch.x = contact.x;
    touch.y = contact.y;
    contact = {};
  }
  void longPress(const int x, const int y) {
    touch.longPress = true;
    touch.x = x;
    touch.y = y;
  }
  // A swipe from (x0, y0) to (x1, y1), read from `gpio` as the games canvas reads it.
  void swipe(const float x0, const float y0, const float x1, const float y1) {
    gpio.swipe = {true, x0, y0, x1, y1};
    touch.released = true;  // a swipe ends in a raw release the tap classifier never reports
    touch.x = -1;
    touch.y = -1;
  }
  // A swipe as wasSwipe() reports it, for a list that scrolls on one.
  void swipeDirection(const SwipeDir dir) {
    swipeDir = dir;
    touch.released = true;
    touch.x = -1;
    touch.y = -1;
  }
  // The frame is over: nothing is pressed or touched any more.
  void clear() {
    pressed.clear();
    released.clear();
    longPressed.clear();
    heldMs = 0;
    touch = Touch{};
    swipeDir = SwipeDir::None;
    backGesture = homeGesture = menuGesture = false;
    gpio.swipe = HalGPIO::Swipe{};
  }

  std::set<Button> pressed;
  std::set<Button> released;
  std::set<Button> longPressed;
  unsigned long heldMs = 0;
  Touch touch;
  // holdTouch()'s contact, which clear() keeps: a held finger is no per-frame event.
  struct Contact {
    bool held = false;
    int x = 0;
    int y = 0;
  } contact;
  SwipeDir swipeDir = SwipeDir::None;
  bool backGesture = false;
  bool homeGesture = false;
  bool menuGesture = false;

 private:
  bool at(int& x, int& y) const {
    x = touch.x;
    y = touch.y;
    return true;
  }

  // Buttons whose long press has fired in this hold, and whose release is suppressed (the real manager's
  // longPressFiredButtons and suppressedReleaseButtons); a const method sets them, as the real one does.
  mutable std::set<Button> firedLongPress;
  mutable std::set<Button> suppressedRelease;

  HalGPIO& gpio;
  const GfxRenderer* renderer;
};

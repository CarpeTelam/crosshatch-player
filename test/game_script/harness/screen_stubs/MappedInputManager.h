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
  bool wasLongPressed(const Button button, unsigned long) const { return longPressed.count(button) != 0; }
  bool consumeSuppressedRelease() const { return false; }
  bool isPressed(const Button button) const { return pressed.count(button) != 0; }
  bool hasTouch() const { return true; }

  bool wasScreenTapped(int& x, int& y) const { return touch.tapped ? at(x, y) : false; }
  bool wasScreenTouchDown(int& x, int& y) const { return touch.down ? at(x, y) : false; }
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
  unsigned long getHeldTime() const { return 0; }
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
    touch = Touch{};
    swipeDir = SwipeDir::None;
    backGesture = homeGesture = menuGesture = false;
    gpio.swipe = HalGPIO::Swipe{};
  }

  std::set<Button> pressed;
  std::set<Button> released;
  std::set<Button> longPressed;
  Touch touch;
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

  HalGPIO& gpio;
  const GfxRenderer* renderer;
};

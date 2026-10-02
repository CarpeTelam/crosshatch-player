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

#include <Arduino.h>
#include <HalGPIO.h>
#include <util/HomeButtonInput.h>

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

  // The device's per-pass sample (main.cpp calls it before the activity's loop): a held contact's first sample stamps
  // its touch-down time, whether or not anything reads touch on that pass.
  void update(bool = false) const { sample(); }
  bool wasPressed(const Button button) const {
    return (button == Button::Confirm && (homeAction == HomeButtonAction::Confirm || powerClick)) ||
           pressed.count(button) != 0;
  }
  bool wasReleased(const Button button) const {
    return (button == Button::Confirm && (homeAction == HomeButtonAction::Confirm || powerClick)) ||
           released.count(button) != 0;
  }
  // The home key's action this frame (homeKey()). It stands in for the device's MappedInputManager on a board with a
  // home key: update() runs the key through HomeButtonInput, which reports the configured action for one update (a
  // tap's at its release, or HomeButtonInput::DOUBLE_TAP_MS after it when a double-tap action is set; a long press's
  // 700 ms into the press); a Confirm action
  // makes wasPressed and wasReleased(Confirm) true on that update (MappedInputManager.cpp, wasPressed and wasReleased),
  // and any action but Ignore makes getHeldTime() 0 (the key's press time is not latched on every board: GT911 does
  // not).
  HomeButtonAction homeButtonAction() const { return homeAction; }
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
  // A held contact (holdTouch) is modelled on the device's touch path (src/MappedInputManager.cpp over freeink-sdk's
  // InputManager.cpp), on the harness clock (millis()). The device samples touch only in update(), on the loop task,
  // once a pass, so a contact's touch-down time is the first update after it lands: here, update() (the match tests'
  // frame() calls it) or the first query below, never holdTouch() itself. wasScreenTouchDown is a level, true on every
  // frame while the still finger has been down TOUCH_DOWN_SELECT_DELAY_MS (90) or more (isTouchTapCandidate);
  // wasScreenLongPress is true on every read of the frame on which the contact has been down TOUCH_LONG_PRESS_MS (500)
  // (touchLongPressEvent, one update long), and reading it suppresses the rest of the contact (suppressTouchContact):
  // no touch-down, no held contact, no release, no tap after it; one not read on that frame is lost (clear() ends the
  // frame), the contact goes on unsuppressed, and its lift taps. isScreenTouchHeld is true while the finger is down and
  // not suppressed (isTouchHeldAt); liftTouch() releases it on a frame that reports the release and the tap (unless
  // suppressed) but no touch-down, since the device clears its press on the release update, and sets the touch-only
  // held time (HalGPIO::lastTouchHeldMs). getHeldTime() is MappedInputManager's: a button's hold on a frame with a
  // button pressed or released, else a tap's held time on its frame. A scripted `touch` (tap(), quickTap(),
  // longPress()) is one frame's events, as the test sets them. Not modelled: tap slop, multi-touch, the held-time
  // override's 250 ms life, and the home key's timing (InputManager's 700 ms long press, HomeButtonInput's double-tap
  // wait): homeKey() scripts only the frame its action is reported on (the device-run packet holds them).
  static constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
  static constexpr unsigned long TOUCH_LONG_PRESS_MS = 500;
  bool wasScreenTouchDown(int& x, int& y) const {
    sample();
    if (contact.held && !contact.suppressed && millis() - contact.sinceMs >= TOUCH_DOWN_SELECT_DELAY_MS) {
      x = contact.x;
      y = contact.y;
      return true;
    }
    return touch.down ? at(x, y) : false;
  }
  bool wasScreenLongPress(int& x, int& y) const {
    sample();
    if (contact.held && !contact.longPressFired && !contact.suppressed &&
        millis() - contact.sinceMs >= TOUCH_LONG_PRESS_MS) {
      contact.longPressFired = true;
      contact.longPressThisFrame = true;
    }
    if (contact.longPressThisFrame) {
      contact.suppressed = true;  // the real wasScreenLongPress consumes it: suppressTouchContact()
      x = contact.x;
      y = contact.y;
      return true;
    }
    return touch.longPress ? at(x, y) : false;
  }
  bool isScreenTouchHeld(int& x, int& y) const {
    sample();
    if (contact.held && !contact.suppressed) {
      x = contact.x;
      y = contact.y;
      return true;
    }
    return touch.held ? at(x, y) : false;
  }
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
  // MappedInputManager::getHeldTime's precedence: a button pressed or released this frame answers its hold (the button
  // hold a test set); else a tap's frame answers its contact's held time (rememberTouchHeldTime); else the button hold.
  // More permissive than the device: the double's `pressed` is a level too (hold()), where the device's is an edge.
  unsigned long getHeldTime() const {
    if (homeAction != HomeButtonAction::Ignore) return 0;  // a mapped action has no contact duration
    if (!pressed.empty() || !released.empty()) return heldMs;
    return touch.tapped ? touch.heldMs : heldMs;
  }
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
    unsigned long heldMs = 0;  // getHeldTime() on a tap's frame
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
  // The home key reports `action` this frame, as HomeButtonInput hands it to the device's manager (homeButtonAction).
  void homeKey(const HomeButtonAction action) { homeAction = action; }
  // The X4 Pro's power click as Confirm, this frame. It stands in for the device with the power button set to Confirm
  // and the double-click frontlight on (src/main.cpp, setPowerConfirmClickFrame; MappedInputManager.cpp
  // wasPowerConfirmClick): a click held at most 300 ms becomes Confirm on the first update more than 500 ms after its
  // release, an update with no button edge, so wasPressed and wasReleased(Confirm) are true, wasAnyPressed and
  // wasAnyReleased false, and getHeldTime() answers InputManager's last whole press of any button
  // (buttonPressFinish less buttonPressStart): `staleHeldMs`, long over.
  void powerConfirmClick(const unsigned long staleHeldMs) {
    powerClick = true;
    heldMs = staleHeldMs;
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
    gpio.touchHeldMs = touch.heldMs;  // the release update sets the last contact's held time (0 here)
  }
  // A finger put down still at (x, y) now, held across frames (and clear()) until liftTouch(): see wasScreenTouchDown.
  void holdTouch(const int x, const int y) {
    contact = Contact{};
    contact.held = true;
    contact.x = x;
    contact.y = y;
  }
  // The held finger lifts: the release this frame, and a tap at its touch-down point with its held time unless the
  // contact was suppressed; the release update sets the held time either way. A contact no update sampled (it came and
  // went while the loop task was blocked) is one the device never sees: nothing. Nothing without a held contact.
  void liftTouch() {
    if (!contact.held || !contact.sampled) {
      contact = Contact{};
      return;
    }
    gpio.touchHeldMs = millis() - contact.sinceMs;
    if (!contact.suppressed) {
      touch.tapped = touch.released = true;
      touch.x = contact.x;
      touch.y = contact.y;
      touch.heldMs = gpio.touchHeldMs;
    }
    contact = Contact{};
  }
  // A tap shorter than 90 ms, as the device reports it: the release and the tap on one frame, and never a touch-down
  // (tap() also scripts the touch-down, as a screen double needs for rowTouch's Down).
  void quickTap(const int x, const int y, const unsigned long heldMs = 50) {
    touch.tapped = touch.released = true;
    touch.x = x;
    touch.y = y;
    touch.heldMs = heldMs;
    gpio.touchHeldMs = heldMs;
  }
  // A contact (held, or this frame's) that ends with no gesture: on the device, one whose excursion passed the 59 px
  // tap-release slop and that made no swipe (it came back, or took longer than a swipe may). A raw release only (none
  // for a suppressed contact).
  void liftWithoutTap() {
    if (contact.held && contact.sampled) gpio.touchHeldMs = millis() - contact.sinceMs;
    if (!contact.suppressed) {
      touch.released = true;
      touch.x = -1;
      touch.y = -1;
    }
    contact = Contact{};
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
    // A long press due this frame and not read is lost, as the device's one-update event is; one reported is over.
    if (contact.held && contact.sampled && !contact.suppressed && millis() - contact.sinceMs >= TOUCH_LONG_PRESS_MS) {
      contact.longPressFired = true;
    }
    contact.longPressThisFrame = false;
    pressed.clear();
    released.clear();
    longPressed.clear();
    heldMs = 0;
    homeAction = HomeButtonAction::Ignore;
    powerClick = false;
    touch = Touch{};
    swipeDir = SwipeDir::None;
    backGesture = homeGesture = menuGesture = false;
    gpio.swipe = HalGPIO::Swipe{};
  }

  std::set<Button> pressed;
  std::set<Button> released;
  std::set<Button> longPressed;
  unsigned long heldMs = 0;
  HomeButtonAction homeAction = HomeButtonAction::Ignore;
  bool powerClick = false;  // powerConfirmClick()
  Touch touch;
  // holdTouch()'s contact, which clear() keeps: a held finger is no per-frame event. Mutable: the real manager's
  // long-press read suppresses the contact from a const method.
  struct Contact {
    bool held = false;
    bool sampled = false;  // a read has stamped sinceMs (the device's first sample of the contact)
    bool suppressed = false;
    bool longPressFired = false;
    bool longPressThisFrame = false;  // the frame on which the long press is reported, on every read
    int x = 0;
    int y = 0;
    unsigned long sinceMs = 0;
  };
  mutable Contact contact;
  SwipeDir swipeDir = SwipeDir::None;
  bool backGesture = false;
  bool homeGesture = false;
  bool menuGesture = false;

 private:
  // The device's first sample of a held contact stamps its touch-down time.
  void sample() const {
    if (contact.held && !contact.sampled) {
      contact.sampled = true;
      contact.sinceMs = millis();
    }
  }
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

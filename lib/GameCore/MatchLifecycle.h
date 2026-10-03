#pragma once

#include <cstdint>

namespace GameCore {

// AD-21's match states. Starting lasts until the VM has started or the game failed to
// load; Leaving is terminal. Result and HandOff are a hidden pass match's only: Result
// shows the seat that just moved its own view until it taps, and HandOff, which shows no
// seat, waits for the next player to take the device and tap.
enum class MatchState : uint8_t { Starting, Playing, Paused, Over, Error, Leaving, Result, HandOff };

// What can happen to a match. Back and Home are the Back button or gesture and the
// Home key or gesture; Resume, Leave, and PlayAgain are menu choices; RoundOver
// means the status the game shipped is over; ScriptError covers a load or start
// failure, a ScriptError, and a stuck script (AD-14); ForcedExit is onExit() without
// a user exit (sleep, or any Replace); TurnChanged means an accepted move changed the
// turn seat; Tap is a tap on the Result banner or the hand-off screen.
enum class MatchEvent : uint8_t {
  Started,
  Back,
  Home,
  Resume,
  Leave,
  RoundOver,
  PlayAgain,
  ScriptError,
  ForcedExit,
  TurnChanged,
  Tap
};

// The choices a state's menu offers, in order, as the events they raise.
struct MatchMenu {
  const MatchEvent* events = nullptr;
  uint8_t count = 0;
};

// AD-21's state machines (AD-20 owns what each transition does). The solo machine, which
// an open pass match also runs:
//
//   Starting --Started--> Playing        Paused --Resume or Back--> Playing
//   Playing --Back or Home--> Paused     Paused --Leave--> Leaving
//   Playing --RoundOver--> Over          Over --PlayAgain--> Playing
//   Over --Leave--> Leaving              Error --Back--> Leaving
//   every state but Error and Leaving --ScriptError--> Error
//   every state but Leaving --ForcedExit--> Leaving
//
// A hidden pass match's machine is the solo one with these changes and additions:
//
//   Starting --Started--> HandOff        Over --PlayAgain--> HandOff
//   Playing --TurnChanged--> Result      Result --Tap--> HandOff
//   HandOff --Tap--> Playing             Result or HandOff --Back or Home--> Paused
//   Paused --Resume or Back--> the state it was entered from (Playing, Result, or HandOff)
//
// A move that ends the round raises RoundOver from Playing, never TurnChanged: RoundOver
// is not a transition from Result or HandOff (AD-21 has no Result to Over), so a winning
// move reported as a turn change would lose the round's end.
//
// Every other event is not a transition and leaves the state as it is: Back and
// Home never leave a match directly, Back in the pause menu closes it, and the solo
// machine never reaches Result or HandOff (with the flag off, neither has a row but
// ForcedExit, and Paused always returns to Playing). Pure; the match drives it from its
// loop task.
class MatchLifecycle {
 public:
  // The solo machine (solo and open pass).
  MatchLifecycle() = default;
  // The hidden pass machine when `hiddenPass`; fixed for the match's life.
  explicit MatchLifecycle(const bool hiddenPass) : hidden(hiddenPass) {}

  MatchState state() const { return current; }
  bool hiddenPass() const { return hidden; }
  // The state Paused returns to on Resume or Back: the one it was entered from; Playing
  // outside Paused, and always Playing with the flag off.
  MatchState resumesTo() const { return current == MatchState::Paused ? pausedFrom : MatchState::Playing; }
  // True when `event` is a transition from the current state.
  bool allows(MatchEvent event) const { return next(current, event, hidden, pausedFrom) != current; }
  // Takes the transition `event` names; false (state unchanged) when there is none.
  bool apply(MatchEvent event);

  // The state `event` leads to from `from` in the hidden pass machine when `hiddenPass`,
  // else the solo one; `from` itself when it is not a transition. `resumesTo` is where
  // Paused returns, honoured only when `hiddenPass` and it is Result or HandOff (Playing
  // otherwise).
  static MatchState next(MatchState from, MatchEvent event, bool hiddenPass = false,
                         MatchState resumesTo = MatchState::Playing);
  // The pause menu (Resume, Leave), the end-of-round menu (PlayAgain, Leave), and
  // the error view's one control (Back); empty for the other states.
  static MatchMenu menuFor(MatchState state);
  static const char* name(MatchState state);
  static const char* name(MatchEvent event);

 private:
  MatchState current = MatchState::Starting;
  bool hidden = false;
  // The state Paused was entered from.
  MatchState pausedFrom = MatchState::Playing;
};

}  // namespace GameCore

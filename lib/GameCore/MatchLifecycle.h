#pragma once

#include <cstdint>

namespace GameCore {

// AD-21's solo match states. Starting lasts until the VM has started or the game
// failed to load; Leaving is terminal.
enum class MatchState : uint8_t { Starting, Playing, Paused, Over, Error, Leaving };

// What can happen to a match. Back and Home are the Back button or gesture and the
// Home key or gesture; Resume, Leave, and PlayAgain are menu choices; RoundOver
// means the status the game shipped is over; ScriptError covers a load or start
// failure, a ScriptError, and a stuck script (AD-14); ForcedExit is onExit() without
// a user exit (sleep, or any Replace).
enum class MatchEvent : uint8_t { Started, Back, Home, Resume, Leave, RoundOver, PlayAgain, ScriptError, ForcedExit };

// The choices a state's menu offers, in order, as the events they raise.
struct MatchMenu {
  const MatchEvent* events = nullptr;
  uint8_t count = 0;
};

// AD-21's solo state machine (AD-20 owns what each transition does):
//
//   Starting --Started--> Playing        Paused --Resume or Back--> Playing
//   Playing --Back or Home--> Paused     Paused --Leave--> Leaving
//   Playing --RoundOver--> Over          Over --PlayAgain--> Playing
//   Over --Leave--> Leaving              Error --Back--> Leaving
//   every state but Error and Leaving --ScriptError--> Error
//   every state but Leaving --ForcedExit--> Leaving
//
// Every other event is not a transition and leaves the state as it is: Back and
// Home never leave a match directly, and Back in the pause menu closes it. Pure;
// the match drives it from its loop task.
class MatchLifecycle {
 public:
  MatchState state() const { return current; }
  // True when `event` is a transition from the current state.
  bool allows(MatchEvent event) const { return next(current, event) != current; }
  // Takes the transition `event` names; false (state unchanged) when there is none.
  bool apply(MatchEvent event);

  // The state `event` leads to from `from`; `from` itself when it is not a transition.
  static MatchState next(MatchState from, MatchEvent event);
  // The pause menu (Resume, Leave), the end-of-round menu (PlayAgain, Leave), and
  // the error view's one control (Back); empty for the other states.
  static MatchMenu menuFor(MatchState state);
  static const char* name(MatchState state);
  static const char* name(MatchEvent event);

 private:
  MatchState current = MatchState::Starting;
};

}  // namespace GameCore

#include "MatchLifecycle.h"

namespace GameCore {

namespace {

constexpr MatchEvent PAUSE_MENU[] = {MatchEvent::Resume, MatchEvent::Leave};
constexpr MatchEvent ROUND_OVER_MENU[] = {MatchEvent::PlayAgain, MatchEvent::Leave};
constexpr MatchEvent ERROR_MENU[] = {MatchEvent::Back};

template <uint8_t N>
constexpr MatchMenu menuOf(const MatchEvent (&events)[N]) {
  return MatchMenu{events, N};
}

}  // namespace

bool MatchLifecycle::apply(const MatchEvent event) {
  const MatchState to = next(current, event, hidden, pausedFrom);
  if (to == current) return false;
  if (to == MatchState::Paused) pausedFrom = current;
  current = to;
  return true;
}

MatchState MatchLifecycle::next(const MatchState from, const MatchEvent event, const bool hiddenPass,
                                const MatchState resumesTo) {
  if (from == MatchState::Leaving) return from;
  if (event == MatchEvent::ForcedExit) return MatchState::Leaving;
  // Where a hidden pass match's round starts: the device is handed to the first mover.
  const MatchState roundStart = hiddenPass ? MatchState::HandOff : MatchState::Playing;
  switch (from) {
    case MatchState::Starting:
      if (event == MatchEvent::Started) return roundStart;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Playing:
      if (event == MatchEvent::Back || event == MatchEvent::Home) return MatchState::Paused;
      if (event == MatchEvent::RoundOver) return MatchState::Over;
      if (event == MatchEvent::TurnChanged && hiddenPass) return MatchState::Result;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Paused:
      if (event == MatchEvent::Resume || event == MatchEvent::Back) {
        // Only the hidden pass machine pauses from Result or HandOff; the solo one always resumes play.
        const bool handOffState = resumesTo == MatchState::Result || resumesTo == MatchState::HandOff;
        return hiddenPass && handOffState ? resumesTo : MatchState::Playing;
      }
      if (event == MatchEvent::Leave) return MatchState::Leaving;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Over:
      if (event == MatchEvent::PlayAgain) return roundStart;
      if (event == MatchEvent::Leave) return MatchState::Leaving;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Error:
      if (event == MatchEvent::Back) return MatchState::Leaving;
      return from;
    case MatchState::Leaving:
      return from;
    case MatchState::Result:
      // The solo machine has no Result row: only ForcedExit, above, leaves it.
      if (!hiddenPass) return from;
      if (event == MatchEvent::Back || event == MatchEvent::Home) return MatchState::Paused;
      if (event == MatchEvent::Tap) return MatchState::HandOff;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::HandOff:
      // The solo machine has no HandOff row: only ForcedExit, above, leaves it.
      if (!hiddenPass) return from;
      if (event == MatchEvent::Back || event == MatchEvent::Home) return MatchState::Paused;
      if (event == MatchEvent::Tap) return MatchState::Playing;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
  }
  return from;
}

MatchMenu MatchLifecycle::menuFor(const MatchState state) {
  switch (state) {
    case MatchState::Paused:
      return menuOf(PAUSE_MENU);
    case MatchState::Over:
      return menuOf(ROUND_OVER_MENU);
    case MatchState::Error:
      return menuOf(ERROR_MENU);
    case MatchState::Starting:
    case MatchState::Playing:
    case MatchState::Leaving:
    case MatchState::Result:
    case MatchState::HandOff:
      return MatchMenu{};
  }
  return MatchMenu{};
}

const char* MatchLifecycle::name(const MatchState state) {
  switch (state) {
    case MatchState::Starting:
      return "Starting";
    case MatchState::Playing:
      return "Playing";
    case MatchState::Paused:
      return "Paused";
    case MatchState::Over:
      return "Over";
    case MatchState::Error:
      return "Error";
    case MatchState::Leaving:
      return "Leaving";
    case MatchState::Result:
      return "Result";
    case MatchState::HandOff:
      return "HandOff";
  }
  return "?";
}

const char* MatchLifecycle::name(const MatchEvent event) {
  switch (event) {
    case MatchEvent::Started:
      return "Started";
    case MatchEvent::Back:
      return "Back";
    case MatchEvent::Home:
      return "Home";
    case MatchEvent::Resume:
      return "Resume";
    case MatchEvent::Leave:
      return "Leave";
    case MatchEvent::RoundOver:
      return "RoundOver";
    case MatchEvent::PlayAgain:
      return "PlayAgain";
    case MatchEvent::ScriptError:
      return "ScriptError";
    case MatchEvent::ForcedExit:
      return "ForcedExit";
    case MatchEvent::TurnChanged:
      return "TurnChanged";
    case MatchEvent::Tap:
      return "Tap";
  }
  return "?";
}

}  // namespace GameCore

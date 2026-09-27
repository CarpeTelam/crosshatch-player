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
  const MatchState to = next(current, event);
  if (to == current) return false;
  current = to;
  return true;
}

MatchState MatchLifecycle::next(const MatchState from, const MatchEvent event) {
  if (from == MatchState::Leaving) return from;
  if (event == MatchEvent::ForcedExit) return MatchState::Leaving;
  switch (from) {
    case MatchState::Starting:
      if (event == MatchEvent::Started) return MatchState::Playing;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Playing:
      if (event == MatchEvent::Back || event == MatchEvent::Home) return MatchState::Paused;
      if (event == MatchEvent::RoundOver) return MatchState::Over;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Paused:
      if (event == MatchEvent::Resume || event == MatchEvent::Back) return MatchState::Playing;
      if (event == MatchEvent::Leave) return MatchState::Leaving;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Over:
      if (event == MatchEvent::PlayAgain) return MatchState::Playing;
      if (event == MatchEvent::Leave) return MatchState::Leaving;
      if (event == MatchEvent::ScriptError) return MatchState::Error;
      return from;
    case MatchState::Error:
      if (event == MatchEvent::Back) return MatchState::Leaving;
      return from;
    case MatchState::Leaving:
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
  }
  return "?";
}

}  // namespace GameCore

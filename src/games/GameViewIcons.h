#pragma once

#include <MatchLifecycle.h>

// The library icons the match's own views draw (docs/crosshatch/game-canvas.md):
// each view's icon in its option dialog's content band, and each menu row's icon
// left of its label, both by name through drawGameIcon (GameIconDraw.h). Pure and
// header-only; GameViewIconsTest checks every name against the library.
namespace GameViewIcons {

// A view's icon, square, centred in the dialog's content band (GameIcons::MEDIUM_PIXELS).
inline constexpr int VIEW_PIXELS = 64;
// A row's icon, square, at the row's left (GameIcons::SMALL_PIXELS).
inline constexpr int ROW_PIXELS = 32;

// The icon of the view `state` shows; null for a state with no view.
constexpr const char* forView(const GameCore::MatchState state) {
  switch (state) {
    case GameCore::MatchState::Paused:
      return "pause";
    case GameCore::MatchState::Over:
      return "flag_checkered";
    case GameCore::MatchState::Error:
      return "warning";
    case GameCore::MatchState::Starting:
    case GameCore::MatchState::Playing:
    case GameCore::MatchState::Leaving:
      break;  // no view
  }
  return nullptr;
}

// The icon of the menu row that raises `event`; null for an event that is never a
// menu choice (MatchLifecycle::menuFor). Back is only the error view's row, which
// leaves the match.
constexpr const char* forOption(const GameCore::MatchEvent event) {
  switch (event) {
    case GameCore::MatchEvent::Resume:
      return "play";
    case GameCore::MatchEvent::Leave:
    case GameCore::MatchEvent::Back:
      return "leave";
    case GameCore::MatchEvent::PlayAgain:
      return "restart";
    case GameCore::MatchEvent::Started:
    case GameCore::MatchEvent::Home:
    case GameCore::MatchEvent::RoundOver:
    case GameCore::MatchEvent::ScriptError:
    case GameCore::MatchEvent::ForcedExit:
      break;  // never a menu choice
  }
  return nullptr;
}

// The top of option row `index` in a vertical fui::optionDialog whose content band
// ends at `bandBottom`: the rows start `gap` below the band, `rowHeight` tall and
// `gap` apart (GameViewIconsTest checks this against optionDialog itself).
constexpr int rowTop(const int bandBottom, const int index, const int rowHeight, const int gap) {
  return bandBottom + gap + index * (rowHeight + gap);
}

// A row icon's inset from the row's top and left edges: the row's vertical margin
// around the icon, 0 for a row shorter than the icon.
constexpr int rowIconInset(const int rowHeight) { return rowHeight > ROW_PIXELS ? (rowHeight - ROW_PIXELS) / 2 : 0; }

// True when a row `rowWidth` x `rowHeight` whose label, `labelWidth` wide, is centred
// in it leaves the inset, the icon, and `gap` free left of the label.
constexpr bool rowIconFits(const int rowWidth, const int rowHeight, const int labelWidth, const int gap) {
  if (rowHeight < ROW_PIXELS || labelWidth > rowWidth) return false;
  const int labelLeft = (rowWidth - labelWidth) / 2;
  return labelLeft >= rowIconInset(rowHeight) + ROW_PIXELS + gap;
}

}  // namespace GameViewIcons

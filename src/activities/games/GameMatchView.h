#pragma once

#include <I18n.h>
#include <MatchLifecycle.h>

#include <cstdint>

#include "components/UiAppHost.h"

class GfxRenderer;

// The match's own views, built on the render task: the pause menu, the end-of-round menu, and the error view (one
// option dialog), Result's framed "Tap to pass" banner, and HandOff's "Player N's turn" line with its "I'm ready"
// button (docs/crosshatch/game-canvas.md). GameMatchActivity owns one, fills an Input at the time FreeInkUI calls it
// back, and keeps the lifecycle, the loops, and what is on the panel; nothing here reads the VM or changes a state.
class GameMatchView {
 public:
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  // A tap on the Result banner or on the hand-off screen's "I'm ready" button.
  static constexpr freeink::ui::ActionId ACTION_PASS = 2;
  // The most options a view offers (the pause and end-of-round menus).
  static constexpr uint8_t MAX_OPTIONS = 2;

  // What the view reads of the match, as it is when the build runs (the render task, inside renderUi).
  struct Input {
    // The state whose view is being drawn (the activity's viewState).
    GameCore::MatchState state = GameCore::MatchState::Starting;
    const char* title = "";  // the manifest's name
    uint8_t focused = 0;     // the focused option (the activity's selected)
    StrId errorHeadline = StrId::STR_GAMES_ERROR;
    const char* errorDetail = "";  // the error view's text, written once before the match enters Error
    // Paused in the Play-again gap: the pause menu says the round is starting (the activity's pauseInGap()).
    bool pauseInGap = false;
    uint8_t passTo = 0;       // the seat the last turn-passing move passed to, for the Result banner
    uint8_t handOffSeat = 0;  // the seat the hand-off screen names
  };

  // Builds the view for `input.state` into `screen`: nothing for a state with no view.
  void build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input);

 private:
  // Result's framed "Tap to pass" banner at the bottom, or HandOff's "Player N's turn" (plain text in the title
  // screen's first row's place) and its "I'm ready" button (filling the second row's): each the screen's one tap target
  // (ACTION_PASS).
  void buildHandOffView(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input);
  // Draws the view's library icon centred in the dialog's content band `band`, and
  // each of the `count` rows' icons at the row's left when its label leaves room
  // (GameViewIcons), in the ink of the row's label. Nothing for an empty band.
  void drawViewIcons(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, freeink::ui::Rect band,
                     GameCore::MatchState state, const GameCore::MatchMenu& menu, uint8_t count) const;
  // The view's translated headline; null for a state with no view.
  static const char* viewHeadline(const Input& input);

  // Render task only: the view's dialog, a member since it is over 1 KB.
  freeink::ui::OptionDialogProps dialogProps;
  // Render task only: Result's banner or the hand-off's "I'm ready" button, and the banner's or the hand-off's line
  // ("Player N's turn"), members since the props are over 256 bytes.
  freeink::ui::ButtonProps bannerProps;
  char bannerText[96] = {};
  // Render task only: GameSplashLayout::rowRect's scratch, the resolved props it measures the hand-off screen's rows
  // with (rowRect still builds a by-value ListProps temporary, as syncListViewport does).
  freeink::ui::ListProps menuProps;
};

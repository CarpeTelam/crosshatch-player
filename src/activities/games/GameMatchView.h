#pragma once

#include <I18n.h>
#include <MatchLifecycle.h>
#include <Protocol.h>

#include <cstdint>

#include "components/UiAppHost.h"

class GfxRenderer;

// The match's own views, built on the render task: the pause menu, the end-of-round menu, and the error view (one
// option dialog) and the player-left view (the same dialog, Back alone), Result's framed "Tap to pass" banner, and
// HandOff's "Player N's turn" line with its "I'm ready" button (docs/crosshatch/game-canvas.md). GameMatchActivity owns
// one, fills an Input at the time FreeInkUI calls it back, and keeps the lifecycle, the loops, and what is on the
// panel; nothing here reads the VM or changes a state.
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
    // Why a nearby match ended, for the player-left view (PeerGone): its one reason line is the headline. Written once
    // before the match enters PeerGone.
    GameCore::Protocol::AbortReason endReason = GameCore::Protocol::AbortReason::Left;
    // Paused in the Play-again gap: the pause menu says the round is starting (the activity's pauseInGap()).
    bool pauseInGap = false;
    uint8_t passTo = 0;       // the seat the last turn-passing move passed to, for the Result banner
    uint8_t handOffSeat = 0;  // the seat the hand-off screen names
    // A nearby match's device that does not hold seat 1 (the activity's followsHost()): its end-of-round menu offers
    // only Leave, under "Waiting for the host" (the host's Play again starts the next round).
    bool followsHost = false;
    // The follower's screen before the host's first STATE: "Waiting for the host" centred, no controls. Replaces the
    // view of `state`.
    bool waiting = false;
  };

  // The player-left view's reason line for `reason`: left (and full), the game's error, silence, or a different
  // version.
  static StrId peerGoneLine(GameCore::Protocol::AbortReason reason);

  // The choices `state`'s menu offers, in order, as the events they raise: MatchLifecycle::menuFor, except that a
  // nearby follower's end-of-round menu is Leave alone. The one definition the loop (which routes the choice) and the
  // build (which draws it) share.
  static GameCore::MatchMenu menuFor(GameCore::MatchState state, bool followsHost);

  // Builds the view for `input.state` into `screen`: nothing for a state with no view.
  void build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input);

 private:
  // Result's framed "Tap to pass" banner at the bottom, or HandOff's "Player N's turn" (plain text in the title
  // screen's first row's place) and its "I'm ready" button (filling the second row's): each the screen's one tap target
  // (ACTION_PASS).
  void buildHandOffView(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input);
  // The follower's waiting screen: one line of plain text in the middle of the safe area (no frame, no action).
  static void buildWaitingView(UiAppHost::UiScreen& screen);
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

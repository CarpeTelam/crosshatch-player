#if FREEINK_CAP_GAMES

#include "GameMatchView.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "GameSplashLayout.h"
#include "components/UITheme.h"
#include "games/GameIconDraw.h"
#include "games/GameViewIcons.h"

namespace fui = freeink::ui;

namespace {

using GameCore::MatchEvent;
using GameCore::MatchLifecycle;
using GameCore::MatchState;

// Whether an icon beside a label drawn over `paint` in `text` is black (GameViewIcons::labelIsBlack has the rule).
bool labelIsBlack(const fui::Paint& paint, const fui::TextStyle& text) {
  return GameViewIcons::labelIsBlack(paint.kind == fui::PaintKind::Solid, paint.color == fui::Color::White,
                                     GameViewIcons::textInkIsWhite(text.color == fui::Color::White, text.inverted));
}

// A menu choice's label.
StrId optionLabel(const MatchEvent event) {
  switch (event) {
    case MatchEvent::Resume:
      return StrId::STR_GAMES_RESUME;
    case MatchEvent::Leave:
      return StrId::STR_GAMES_LEAVE;
    case MatchEvent::PlayAgain:
      return StrId::STR_GAMES_PLAY_AGAIN;
    case MatchEvent::Back:
      return StrId::STR_BACK;
    case MatchEvent::Started:
    case MatchEvent::Home:
    case MatchEvent::RoundOver:
    case MatchEvent::ScriptError:
    case MatchEvent::ForcedExit:
    case MatchEvent::TurnChanged:
    case MatchEvent::Tap:
      break;  // never a menu choice (MatchLifecycle::menuFor)
  }
  return StrId::STR_BACK;
}

}  // namespace

const char* GameMatchView::viewHeadline(const Input& input) {
  switch (input.state) {
    case MatchState::Paused:
      return tr(STR_GAMES_PAUSED);
    case MatchState::Over:
      return tr(STR_GAMES_OVER);
    case MatchState::Error:
      return I18N.get(input.errorHeadline);
    case MatchState::Starting:
    case MatchState::Playing:
    case MatchState::Leaving:
    case MatchState::Result:   // the banner, which has no headline
    case MatchState::HandOff:  // the hand-off screen, which has no dialog
      break;                   // no view
  }
  return nullptr;
}

void GameMatchView::build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input) {
  const MatchState state = input.state;
  if (state == MatchState::Result || state == MatchState::HandOff) {
    buildHandOffView(screen, renderer, input);
    return;
  }
  const GameCore::MatchMenu menu = MatchLifecycle::menuFor(state);
  const char* headline = viewHeadline(input);
  if (menu.count == 0 || !headline) return;
  const uint8_t count = menu.count < MAX_OPTIONS ? menu.count : MAX_OPTIONS;
  const uint8_t focused = input.focused;
  fui::DialogOption options[MAX_OPTIONS];
  for (uint8_t i = 0; i < count; ++i) {
    options[i].label = I18N.get(optionLabel(menu.events[i]));
    options[i].action = ACTION_OPTION;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = i == focused ? fui::StateFocused : fui::StateNormal;
  }

  const auto& theme = screen.theme();
  fui::OptionDialogProps& props = dialogProps;
  props.title = input.title;
  props.titleText = theme.smallText;
  props.titleText.align = fui::TextAlign::Center;
  props.headline = headline;
  props.headlineText = theme.titleText;
  props.headlineText.align = fui::TextAlign::Center;
  props.headlineText.maxLines = 2;
  // The error view adds Lua's message in small type, wrapped (AD-14); its
  // ERROR_CAPACITY bytes fit in 8 lines. A pause menu in the Play-again gap says
  // the round is starting, since Resume shows nothing new until it has.
  props.message = state == MatchState::Error ? input.errorDetail : nullptr;
  props.messageText = theme.smallText;
  props.messageText.maxLines = 8;
  if (state == MatchState::Paused && input.pauseInGap) {
    // Centred under the headline, like the caption and the headline above it.
    props.message = tr(STR_GAMES_NEXT_ROUND_STARTING);
    props.messageText.align = fui::TextAlign::Center;
  }
  props.buttonText = theme.bodyText;
  props.buttonStyles = theme.button;
  props.options = options;
  props.optionCount = count;
  props.verticalOptions = true;
  // The view's library icon sits in the content band, between the text and the rows.
  props.contentHeight = GameViewIcons::forView(state) ? static_cast<int16_t>(GameViewIcons::VIEW_PIXELS) : 0;
  // Touch only: the buttons are read in loopView().
  props.inputMask = fui::InputTouch;
  // A framed panel, as OptionPopup draws it, so it stands out over the game.
  const auto& metrics = UITheme::getInstance().getMetrics();
  fui::BoxStyle& panel = props.styles.normal;
  panel.background = fui::Paint::solid(fui::Color::White);
  panel.foreground = fui::Paint::solid(fui::Color::Black);
  panel.border = fui::Paint::solid(fui::Color::Black);
  panel.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  panel.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = panel;
  props.styles.focused = panel;
  props.styles.active = panel;
  props.styles.disabled = panel;
  props.styles.explicitlySet = true;

  const fui::Rect safe = screen.frame().safeRect();
  const auto width = static_cast<int16_t>(safe.width * 4 / 5);
  const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
  const fui::Rect band = fui::optionDialog(screen.frame(), fui::centeredRect(safe, fui::Size{width, height}), props);
  drawViewIcons(screen, renderer, band, state, menu, count);
}

void GameMatchView::buildHandOffView(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const Input& input) {
  const MatchState state = input.state;
  const fui::Rect safe = screen.frame().safeRect();
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The banner and the button are each the screen's one tap target, routed like any FreeInkUI control: a tap on it
  // passes the device on (loopHandOff), a tap elsewhere routes nothing. Touch only: Confirm is read in loopHandOff.
  fui::ButtonProps& props = bannerProps;
  props.action = ACTION_PASS;
  props.inputMask = fui::InputTouch;
  props.text = theme.bodyText;
  fui::BoxStyle& panel = props.styles.normal;
  panel.background = fui::Paint::solid(fui::Color::White);
  panel.foreground = fui::Paint::solid(fui::Color::Black);
  panel.border = fui::Paint::solid(fui::Color::Black);
  panel.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  panel.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = panel;
  props.styles.focused = panel;
  props.styles.active = panel;
  props.styles.disabled = panel;
  props.styles.explicitlySet = true;
  const auto width = static_cast<int16_t>(safe.width * 4 / 5);
  const auto left = static_cast<int16_t>(safe.x + (safe.width - width) / 2);
  if (state == MatchState::Result) {
    // A framed panel at the bottom, over the mover's frame, naming who takes the device next.
    snprintf(bannerText, sizeof(bannerText), tr(STR_GAMES_PASS_TO_PLAYER), static_cast<unsigned>(input.passTo));
    props.label = bannerText;
    const auto height = static_cast<int16_t>(2 * theme.rowHeight);
    const fui::Rect rect{left, static_cast<int16_t>(safe.bottom() - height - theme.spaceLg), width, height};
    fui::button(screen.frame(), rect, props);
    return;
  }
  // Where the title screen has its first two rows (GameSplashLayout::rowRect), whichever picture the band shows.
  if (input.handOffSeat != 0) {
    // "Player N's turn", plain text centred in the first row's place: no action, no frame. Seat 0 is no player's turn
    // (a round already over when it began announces it): no line.
    const fui::Rect turnRow = GameSplashLayout::rowRect(screen, renderer, 0, menuProps);
    snprintf(bannerText, sizeof(bannerText), tr(STR_GAMES_PLAYER_TURN), static_cast<unsigned>(input.handOffSeat));
    fui::TextStyle text = theme.bodyText;
    text.align = fui::TextAlign::Center;
    const int16_t lineHeight = screen.target().lineHeight(text.font);
    const fui::Rect line{turnRow.x, static_cast<int16_t>(turnRow.y + (turnRow.height - lineHeight) / 2), turnRow.width,
                         lineHeight};
    screen.target().text(line, bannerText, text);
  }
  // "I'm ready" fills the second two-line row's place (the title screen's with a save), framed as the banner is.
  props.label = tr(STR_GAMES_READY);
  fui::button(screen.frame(), GameSplashLayout::rowRect(screen, renderer, 1, menuProps), props);
}

void GameMatchView::drawViewIcons(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const fui::Rect band,
                                  const MatchState state, const GameCore::MatchMenu& menu, const uint8_t count) const {
  // The dialog has no icon field, so the icons are drawn over the finished dialog.
  if (band.empty()) return;
  const fui::OptionDialogProps& props = dialogProps;
  const char* viewIcon = GameViewIcons::forView(state);
  if (viewIcon) {
    // The icon sits on the panel's own background, not on a button. build() sets that panel white with a solid black
    // foreground, so this reads black; it is the panel's foreground that decides, as a row's button style decides its
    // icon, and the headline's text style stands in only for a foreground that is not solid (none is today).
    drawGameIcon(renderer, viewIcon, band.x + (band.width - GameViewIcons::VIEW_PIXELS) / 2, band.y,
                 GameViewIcons::VIEW_PIXELS, labelIsBlack(props.styles.normal.foreground, props.headlineText));
  }
  // optionDialog stacks the rows directly below the band (verticalOptions).
  const int inset = GameViewIcons::rowIconInset(props.buttonHeight);
  for (uint8_t i = 0; i < count; ++i) {
    const char* rowIcon = GameViewIcons::forOption(menu.events[i]);
    const char* label = props.options[i].label;
    if (!rowIcon || !label) continue;
    const int labelWidth = screen.target().measureText(props.buttonText.font, label, props.buttonText).width;
    if (!GameViewIcons::rowIconFits(band.width, props.buttonHeight, labelWidth, props.gap)) continue;
    const int rowY = GameViewIcons::rowTop(band.bottom(), i, props.buttonHeight, props.gap);
    const fui::State rowState = screen.frame().stateFor(ACTION_OPTION, static_cast<int16_t>(i), props.options[i].state);
    const bool black = labelIsBlack(props.buttonStyles.resolve(rowState).foreground, props.buttonText);
    drawGameIcon(renderer, rowIcon, band.x + inset, rowY + inset, GameViewIcons::ROW_PIXELS, black);
  }
}

#endif  // FREEINK_CAP_GAMES

#if FREEINK_CAP_GAMES

#include "GameSplashLayout.h"

#include <GfxRenderer.h>

#include "GamePicture.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

int GameSplashLayout::bandTop(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return safe.y + metrics.topPadding + metrics.headerHeight;
}

fui::Insets GameSplashLayout::menuMargin(const GfxRenderer& renderer) {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto right = static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width));
  const auto bottom = static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height));
  return fui::Insets{static_cast<int16_t>(bandTop(renderer) + BAND), right, bottom, static_cast<int16_t>(safe.x)};
}

void GameSplashLayout::styleMenu(const UiAppHost::UiScreen& screen, fui::ListProps& props) {
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;  // a long New game line ends in an ellipsis
}

fui::Rect GameSplashLayout::rowRect(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const int index,
                                    fui::ListProps& scratch) {
  // A copy, so the caller's content rect stays as it was: the menu's band is the title screen's list's.
  UiAppHost::UiScreen menu = screen;
  menu.setContentMarginFromScreen(menuMargin(renderer));
  scratch = fui::ListProps{};
  styleMenu(menu, scratch);
  scratch = menu.resolveListProps(scratch);
  // The rows' band inside the list's, as fui::list lays it out: the theme's row inset on both sides, then the scroll
  // strip, which a ListNav-managed list (the title screen's) keeps clear whether or not it draws an indicator. This
  // mirrors fui::list's private layout in freeink-sdk/libs/ui/FreeInkUI/include/components/lists/list.h (at 111fdcc:
  // reserveScrollStrip :525, rowArea :540-557, the row rect and its step :649-650; measureListRow :433): recheck it on
  // an SDK bump (ModePickerTest's row-rect test fails when they part).
  fui::Rect area = menu.contentRect();
  const int16_t inset = scratch.rowInset < 0 ? 0 : scratch.rowInset;
  area.x = static_cast<int16_t>(area.x + inset);
  area.width = static_cast<int16_t>(area.width - inset * 2);
  const int16_t scrollWidth = scratch.scrollIndicatorWidth < 0 ? 3 : scratch.scrollIndicatorWidth;
  const int16_t scrollInset = scratch.scrollIndicatorInset < 0 ? 0 : scratch.scrollIndicatorInset;
  const auto strip = static_cast<int16_t>(scrollWidth + scrollInset + 2);
  if (scratch.scrollIndicator && scrollWidth > 0 && inset < strip) {
    const auto cut = static_cast<int16_t>(strip - inset);
    area.width = static_cast<int16_t>(area.width - cut);
    if (scratch.scrollIndicatorSide == 1) area.x = static_cast<int16_t>(area.x + cut);
  }
  // A row with a label and a second line, no icon and no value, measured as fui::list measures it.
  fui::ListItem item;
  item.label = "X";
  item.subtitle = "X";
  const fui::ListRowLayout row = fui::measureListRow(menu.target(), menu.frame().assets(), area.width, scratch, item);
  const int16_t gap = scratch.rowGap < 0 ? 0 : scratch.rowGap;
  return fui::Rect{area.x, static_cast<int16_t>(area.y + index * (row.height + gap)), area.width, row.height};
}

void GameSplashLayout::drawBand(const GfxRenderer& renderer, const GamePicture& picture) {
  const int top = bandTop(renderer);
  const int width = renderer.getScreenWidth();
  if (picture.hasPage()) {
    picture.drawPage(renderer, 0, top, width, BAND);
  } else {
    picture.drawIcon(renderer, width / 2, top + BAND / 2);
  }
}

#endif  // FREEINK_CAP_GAMES

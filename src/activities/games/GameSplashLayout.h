#pragma once

#include "components/UiAppHost.h"  // UiAppHost::UiScreen, and FreeInkUI's Rect, Insets, and ListProps

class GfxRenderer;
class GamePicture;

// The title screen's splash layout (DESIGN.md, Title-screen splash and menu rows), in one place for the two screens
// that show it: the title screen (GameModeActivity), whose list lays its menu rows under the band, and the hidden pass
// hand-off screen (GameMatchActivity, AD-12 as amended 2026-10-02), which shows the same band and puts "Player N's
// turn" and the "I'm ready" button where the title screen's first two rows are. The geometry comes from the theme the
// screen is built with and from FreeInkUI's own list measurement, so the two cannot drift apart; ModePickerTest pins
// rowRect against the rows the title screen's list draws.
namespace GameSplashLayout {

// The band's height and width under the header (DESIGN.md {spacing.splash-band}): title.png's and handoff.png's
// largest size, so the menu does not move between games.
constexpr int BAND = 480;

// The top of the band on the logical screen: under the header (the safe area's top, the theme's top padding, and the
// header's height), whether or not the screen draws a header.
int bandTop(const GfxRenderer& renderer);
// The margins, from the screen's edges, of the menu under the band (UiScreen::setContentMarginFromScreen): the band's
// bottom on top, the safe area left with front button hints on the other three sides.
freeink::ui::Insets menuMargin(const GfxRenderer& renderer);
// The menu's own list props, before the theme resolves the rest: each second line in the theme's small text, one line,
// ending in an ellipsis when long.
void styleMenu(const UiAppHost::UiScreen& screen, freeink::ui::ListProps& props);
// Render task: the rect of the menu's two-line row `index` (0 first), exactly where the title screen's list lays out a
// row with a label and a second line (Continue, New game): the list's band under the band with the theme's row inset
// and its scroll strip kept clear, as a ListNav-managed list keeps it, the row height FreeInkUI measures for a label
// and a second line, and the theme's row gap. `scratch`, the caller's, holds the resolved props for the
// measurement (resolveListProps still builds one by value on the stack, as syncListViewport's does); `screen` is left
// as it was.
freeink::ui::Rect rowRect(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, int index,
                          freeink::ui::ListProps& scratch);
// Render task: the band's picture, as the title screen draws it: the picture's page centred and clipped in the band,
// else its icon at 128 px centred in it.
void drawBand(const GfxRenderer& renderer, const GamePicture& picture);

}  // namespace GameSplashLayout

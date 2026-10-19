#include "PanelGeometry.h"

namespace GameDisplayLab {

void toPanelPixel(const PanelTurn turn, const int x, const int y, const int panelW, const int panelH, int& px,
                  int& py) {
  switch (turn) {
    case PanelTurn::Portrait:
      px = y;
      py = panelH - 1 - x;
      return;
    case PanelTurn::LandscapeClockwise:
      px = panelW - 1 - x;
      py = panelH - 1 - y;
      return;
    case PanelTurn::PortraitInverted:
      px = panelW - 1 - y;
      py = x;
      return;
    case PanelTurn::LandscapeCounterClockwise:
      px = x;
      py = y;
      return;
  }
  px = x;
  py = y;
}

ScreenRect toPanel(const PanelTurn turn, const ScreenRect& r, const int panelW, const int panelH) {
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  toPanelPixel(turn, r.x, r.y, panelW, panelH, ax, ay);
  toPanelPixel(turn, r.x + r.w - 1, r.y + r.h - 1, panelW, panelH, bx, by);
  const int x0 = ax < bx ? ax : bx;
  const int y0 = ay < by ? ay : by;
  const int x1 = ax < bx ? bx : ax;
  const int y1 = ay < by ? by : ay;
  return ScreenRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

void retargetLogicalRect(GrayModel& model, const LogicalScreen& screen, int x0, int x1, int y, int h, const Shade shade,
                         const GrayPreset& preset) {
  if (x0 < 0) x0 = 0;
  if (x1 > screen.logicalW) x1 = screen.logicalW;
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (y + h > screen.logicalH) h = screen.logicalH - y;
  if (x1 <= x0 || h <= 0) return;
  const ScreenRect panel = toPanel(screen.turn, ScreenRect{x0, y, x1 - x0, h}, screen.panelW, screen.panelH);
  model.retargetRect(panel.x, panel.y, panel.w, panel.h, shade, preset);
}

std::array<ScreenRect, 2> pauseBars(const ScreenRect& box, const int gap) {
  const int bar = (box.w - gap) / 2;
  // Too narrow for two bars and the gap: one bar, the whole rectangle.
  if (bar < 1) return {box, ScreenRect{box.x + box.w, box.y, 0, box.h}};
  return {ScreenRect{box.x, box.y, bar, box.h}, ScreenRect{box.x + box.w - bar, box.y, bar, box.h}};
}

bool barsSettled(const GrayModel& model, const std::array<ScreenRect, 2>& bars, const LogicalScreen& screen) {
  // The bars' pixels only: the gap is never driven.
  for (const ScreenRect& logical : bars) {
    if (logical.w <= 0 || logical.h <= 0) continue;
    const ScreenRect panel = toPanel(screen.turn, logical, screen.panelW, screen.panelH);
    for (int y = panel.y; y < panel.y + panel.h; ++y) {
      for (int x = panel.x; x < panel.x + panel.w; ++x) {
        if (model.remaining(x, y) > 0) return false;
      }
    }
  }
  return true;
}

}  // namespace GameDisplayLab

#include "TransitionScene.h"

namespace GameDisplayLab {

namespace {

// GfxRenderer::rotateCoordinates for one pixel: logical (x, y) to panel (px, py).
void rotate(const PanelTurn turn, const int x, const int y, const int panelW, const int panelH, int& px, int& py) {
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

}  // namespace

ScreenRect toPanel(const PanelTurn turn, const ScreenRect& r, const int panelW, const int panelH) {
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  rotate(turn, r.x, r.y, panelW, panelH, ax, ay);
  rotate(turn, r.x + r.w - 1, r.y + r.h - 1, panelW, panelH, bx, by);
  const int x0 = ax < bx ? ax : bx;
  const int y0 = ay < by ? ay : by;
  const int x1 = ax < bx ? bx : ax;
  const int y1 = ay < by ? by : ay;
  return ScreenRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

TransitionScene::TransitionScene(const PanelTurn turn, const int logicalW, const int logicalH, const int panelW,
                                 const int panelH)
    : turn_(turn), logicalW_(logicalW), logicalH_(logicalH), panelW_(panelW), panelH_(panelH) {}

std::optional<int> TransitionScene::rectX(const uint32_t elapsedMs, const int logicalW) {
  const uint32_t t = elapsedMs % CYCLE_MS;
  if (t >= SWEEP_MS) return std::nullopt;
  const int64_t travel = static_cast<int64_t>(logicalW) + RECT_W;
  return static_cast<int>(-RECT_W + travel * t / SWEEP_MS);
}

int TransitionScene::bandOf(const int ly) const {
  const int band = bandHeight() > 0 ? ly / bandHeight() : BANDS - 1;
  return band < BANDS ? band : BANDS - 1;
}

Shade TransitionScene::targetAt(const int lx, const int ly, const uint32_t elapsedMs) const {
  const int band = bandOf(ly);
  const std::optional<int> x = rectX(elapsedMs, logicalW_);
  if (!x || lx < *x || lx >= *x + RECT_W || rectHeight() <= 0) return bandShade(band);
  for (int lane = 0; lane < LANES; ++lane) {
    const int top = rectTop(band, lane);
    if (ly >= top && ly < top + rectHeight()) return laneShade(band, lane);
  }
  return bandShade(band);
}

void TransitionScene::applyAll(GrayModel& model, const uint32_t elapsedMs, const GrayPreset& preset) const {
  for (int ly = 0; ly < logicalH_; ++ly) {
    for (int lx = 0; lx < logicalW_; ++lx) {
      int px = 0;
      int py = 0;
      rotate(turn_, lx, ly, panelW_, panelH_, px, py);
      model.retarget(px, py, targetAt(lx, ly, elapsedMs), preset);
    }
  }
}

void TransitionScene::retargetLogical(GrayModel& model, int x0, int x1, int y, int h, const Shade shade,
                                      const GrayPreset& preset) const {
  if (x0 < 0) x0 = 0;
  if (x1 > logicalW_) x1 = logicalW_;
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (y + h > logicalH_) h = logicalH_ - y;
  if (x1 <= x0 || h <= 0) return;
  const ScreenRect panel = toPanel(turn_, ScreenRect{x0, y, x1 - x0, h}, panelW_, panelH_);
  model.retargetRect(panel.x, panel.y, panel.w, panel.h, shade, preset);
}

void TransitionScene::update(GrayModel& model, const uint32_t prevMs, const uint32_t nowMs,
                             const GrayPreset& preset) const {
  const std::optional<int> before = rectX(prevMs, logicalW_);
  const std::optional<int> after = rectX(nowMs, logicalW_);
  if (before == after || rectHeight() <= 0) return;
  for (int band = 0; band < BANDS; ++band) {
    for (int lane = 0; lane < LANES; ++lane) {
      const int top = rectTop(band, lane);
      if (before) {
        // The old rectangle's area the new one does not cover goes back to the band: left of it and right of it.
        const int o0 = *before;
        const int o1 = o0 + RECT_W;
        const int n0 = after ? *after : o1;
        const int n1 = after ? *after + RECT_W : o1;
        retargetLogical(model, o0, n0 < o1 ? n0 : o1, top, rectHeight(), bandShade(band), preset);
        retargetLogical(model, n1 > o0 ? n1 : o0, o1, top, rectHeight(), bandShade(band), preset);
      }
      if (after) retargetLogical(model, *after, *after + RECT_W, top, rectHeight(), laneShade(band, lane), preset);
    }
  }
}

}  // namespace GameDisplayLab

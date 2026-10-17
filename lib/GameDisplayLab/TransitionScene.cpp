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

SweepMode nextMode(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return SweepMode::Slow;
    case SweepMode::Slow:
      return SweepMode::Fast;
    case SweepMode::Fast:
      return SweepMode::Static;
  }
  return SweepMode::Static;
}

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

void TransitionScene::setMode(const SweepMode mode, const uint32_t slotPeriodUs, const uint32_t dwellSlots) {
  mode_ = mode;
  restartFlips();
  dwellSlots_ = dwellSlots > 0 ? dwellSlots : 1;
  slowMeasured_ = false;
  slowStartMs_ = 0;
  sweepCapped_ = false;
  switch (mode) {
    case SweepMode::Static:
      rectW_ = logicalW_ / 3 > 0 ? logicalW_ / 3 : 1;
      sweepMs_ = SWEEP_MS;  // unused: the rectangles stand still
      return;
    case SweepMode::Slow:
      rectW_ = RECT_W_SLOW;
      sizeSlow(slotPeriodUs);
      return;
    case SweepMode::Fast:
      rectW_ = RECT_W;
      sweepMs_ = SWEEP_MS;
      return;
  }
}

uint32_t TransitionScene::slowSweepMs(const uint32_t slotPeriodUs, const int logicalW, const uint32_t dwellSlots) {
  // A rectangle RECT_W_SLOW wide covers a pixel for RECT_W_SLOW / (travel / sweep) of the crossing, travel being the
  // width plus the rectangle's: sweep >= dwellSlots * period * travel / RECT_W_SLOW.
  const uint64_t travel = static_cast<uint64_t>(logicalW > 0 ? logicalW : 0) + RECT_W_SLOW;
  const uint64_t perMs = static_cast<uint64_t>(RECT_W_SLOW) * 1000u;
  const uint64_t ms = (static_cast<uint64_t>(dwellSlots) * slotPeriodUs * travel + perMs - 1) / perMs;
  if (ms == 0) return 1;
  return ms > SLOW_SWEEP_MAX_MS ? SLOW_SWEEP_MAX_MS : static_cast<uint32_t>(ms);
}

void TransitionScene::sizeSlow(const uint32_t slotPeriodUs) {
  sweepMs_ = slowSweepMs(slotPeriodUs, logicalW_, dwellSlots_);
  // The rounded-up crossing gives at least the dwell asked for, unless SLOW_SWEEP_MAX_MS cut it.
  sweepCapped_ = dwellSlotsAt(slotPeriodUs) < dwellSlots_;
}

uint32_t TransitionScene::dwellSlotsAt(const uint32_t slotPeriodUs) const {
  const uint64_t travel = static_cast<uint64_t>(logicalW_ > 0 ? logicalW_ : 0) + static_cast<uint64_t>(rectW_);
  if (slotPeriodUs == 0 || travel == 0) return 0;
  return static_cast<uint32_t>(static_cast<uint64_t>(rectW_) * sweepMs_ * 1000u / (travel * slotPeriodUs));
}

std::optional<int> TransitionScene::rectX(const uint32_t elapsedMs) const {
  switch (mode_) {
    case SweepMode::Static:
      return (logicalW_ - rectW_) / 2;
    case SweepMode::Slow:
    case SweepMode::Fast: {
      // Slow waits just off the left edge until the period is measured, then crosses from that slot's clock.
      const bool slow = mode_ == SweepMode::Slow;
      if (slow && !slowMeasured_) return -rectW_;
      const uint32_t since = slow ? (elapsedMs > slowStartMs_ ? elapsedMs - slowStartMs_ : 0) : elapsedMs;
      const uint32_t t = since % (sweepMs_ + PAUSE_MS);
      if (t >= sweepMs_) return std::nullopt;
      const int64_t travel = static_cast<int64_t>(logicalW_) + rectW_;
      return static_cast<int>(-rectW_ + travel * t / sweepMs_);
    }
  }
  return std::nullopt;
}

int TransitionScene::bandOf(const int ly) const {
  const int band = bandHeight() > 0 ? ly / bandHeight() : BANDS - 1;
  return band < BANDS ? band : BANDS - 1;
}

Shade TransitionScene::targetAt(const int lx, const int ly, const uint32_t elapsedMs) const {
  const int band = bandOf(ly);
  const std::optional<int> x = rectX(elapsedMs);
  if (!x || lx < *x || lx >= *x + rectW_ || rectHeight() <= 0) return shown(bandShade(band));
  for (int lane = 0; lane < LANES; ++lane) {
    const int top = rectTop(band, lane);
    if (ly >= top && ly < top + rectHeight()) return shown(laneShade(band, lane));
  }
  return shown(bandShade(band));
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
  const std::optional<int> before = rectX(prevMs);
  const std::optional<int> after = rectX(nowMs);
  if (before == after || rectHeight() <= 0) return;
  for (int band = 0; band < BANDS; ++band) {
    for (int lane = 0; lane < LANES; ++lane) {
      const int top = rectTop(band, lane);
      if (before) {
        // The old rectangle's area the new one does not cover goes back to the band: left of it and right of it.
        const int o0 = *before;
        const int o1 = o0 + rectW_;
        const int n0 = after ? *after : o1;
        const int n1 = after ? *after + rectW_ : o1;
        retargetLogical(model, o0, n0 < o1 ? n0 : o1, top, rectHeight(), shown(bandShade(band)), preset);
        retargetLogical(model, n1 > o0 ? n1 : o0, o1, top, rectHeight(), shown(bandShade(band)), preset);
      }
      if (after) {
        retargetLogical(model, *after, *after + rectW_, top, rectHeight(), shown(laneShade(band, lane)), preset);
      }
    }
  }
}

uint8_t TransitionScene::flipOffset(const uint32_t flip) {
  return flip == 0 ? 0 : static_cast<uint8_t>((flip - 1) % 3 + 1);
}

SceneStep TransitionScene::slotDone(const size_t driven, const bool driveDone, const uint32_t periodUs,
                                    const uint32_t elapsedMs) {
  SceneStep step;
  switch (mode_) {
    case SweepMode::Static:
      if (driven > 0) {
        ++settleSlots_;
        return step;
      }
      // No pixel had a dose left for this slot: the pattern settled. The next flip moves every shade on.
      step.settled = StaticSettled{flips_, flipOffset(flips_), settleSlots_};
      ++flips_;
      shift_ = static_cast<uint8_t>((shift_ + flipOffset(flips_)) % SHADE_COUNT);
      settleSlots_ = 0;
      step.reapply = true;
      return step;
    case SweepMode::Slow:
      if (slowMeasured_ || !driveDone) return step;
      slowMeasured_ = true;
      slowStartMs_ = elapsedMs;
      sizeSlow(periodUs);
      step.measured = true;
      return step;
    case SweepMode::Fast:
      return step;
  }
  return step;
}

void TransitionScene::restartFlips() {
  flips_ = 0;
  shift_ = 0;
  settleSlots_ = 0;
}

}  // namespace GameDisplayLab

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

const char* holdSpotName(const HoldSpot spot) {
  switch (spot) {
    case HoldSpot::StaticSettle:
      return "static settle";
    case HoldSpot::SlowPainted:
      return "slow painted";
    case HoldSpot::SlowDwelt:
      return "slow dwelt";
    case HoldSpot::SlowCrossed:
      return "slow crossed";
    case HoldSpot::FastPause:
      return "fast pause";
  }
  return "";
}

char shadeLetter(const Shade shade) {
  switch (shade) {
    case Shade::K:
      return 'K';
    case Shade::D:
      return 'D';
    case Shade::L:
      return 'L';
    case Shade::W:
      return 'W';
  }
  return '?';
}

const char* spotSkipName(const SpotSkip skip) {
  switch (skip) {
    case SpotSkip::RightEdge:
      return "the rectangles reached the right edge first";
    case SpotSkip::PauseOutrun:
      return "no slot started in the pause";
  }
  return "";
}

const char* holdDropName(const HoldDrop drop) {
  switch (drop) {
    case HoldDrop::StaticAgainAtFlipZero:
      return "holds again at flip 0";
    case HoldDrop::SkippedThisCrossing:
      return "skipped for this crossing";
    case HoldDrop::WayBackCutOff:
      return "photo taken, the marker's way back cut off";
  }
  return "";
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

void TransitionScene::setMode(const SweepMode mode, const uint32_t dwellSlots) {
  mode_ = mode;
  restartFlips();
  dwellSlots_ = dwellSlots > 0 ? dwellSlots : 1;
  slowMeasured_ = false;
  slowPeriodUs_ = 0;
  slowStartMs_ = 0;
  sweepCapped_ = false;
  dropHold();
  resetCrossingSpots(0);
  switch (mode) {
    case SweepMode::Static:
      rectW_ = logicalW_ / 3 > 0 ? logicalW_ / 3 : 1;
      sweepMs_ = SWEEP_MS;  // unused: the rectangles stand still
      return;
    case SweepMode::Slow:
      rectW_ = RECT_W_SLOW;
      sweepMs_ = 0;  // sized by slotDone once the paint has settled; rectX holds the rectangles until then
      return;
    case SweepMode::Fast:
      rectW_ = RECT_W;
      sweepMs_ = SWEEP_MS;
      return;
  }
}

void TransitionScene::setHold(const bool on) {
  holdOn_ = on;
  dropHold();
}

void TransitionScene::dropHold() {
  phase_ = HoldPhase::Running;
  markerOut_ = false;
  markerSlots_ = 0;
  flipPending_ = false;
  spot_.reset();
}

void TransitionScene::resetCrossingSpots(const uint32_t cycle) {
  cycle_ = cycle;
  enteredSlots_ = 0;
  dwellPassed_ = false;
  crossPassed_ = false;
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
      if (mode_ == SweepMode::Slow && !slowMeasured_) return -rectW_;
      const uint32_t t = crossingAt(elapsedMs).ms;
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
  if (markerOut_) {
    const ScreenRect m = markerRect();
    if (lx >= m.x && lx < m.x + m.w && ly >= m.y && ly < m.y + m.h) return markerShade();
  }
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

SceneStep TransitionScene::slotDone(GrayModel& model, const GrayPreset& preset, const size_t driven,
                                    const bool driveDone, const uint32_t periodUs, const uint32_t elapsedMs) {
  SceneStep step;
  switch (phase_) {
    case HoldPhase::Held:
      return step;  // the task runs no slot while the run holds
    case HoldPhase::MarkerOut:
      if (!markerArrived(model, step)) return step;
      phase_ = HoldPhase::Held;
      step.held = true;
      return step;
    case HoldPhase::MarkerBack:
      if (!markerArrived(model, step)) return step;
      phase_ = HoldPhase::Running;
      step.resumed = true;
      if (flipPending_) {
        // Static: the flip the settle held back runs now, over a marker back on its band's shade.
        flipPending_ = false;
        flip();
        step.reapply = true;
      }
      return step;
    case HoldPhase::Running:
      break;
  }
  const std::optional<HoldStart> spot = patternStep(step, driven, driveDone, periodUs, elapsedMs);
  if (!spot || !holdOn_) return step;
  // A photo spot: the marker out to its hold shade, driven by the next slots at a still clock.
  phase_ = HoldPhase::MarkerOut;
  markerOut_ = true;
  spot_ = spot->spot;
  markerSlots_ = 0;
  retargetMarker(model, preset);
  step.hold = spot;
  return step;
}

std::optional<HoldStart> TransitionScene::patternStep(SceneStep& step, const size_t driven, const bool driveDone,
                                                      const uint32_t periodUs, const uint32_t elapsedMs) {
  switch (mode_) {
    case SweepMode::Static:
      if (driven > 0) {
        ++settleSlots_;
        return std::nullopt;
      }
      // No pixel had a dose left for this slot: the pattern settled. The next flip moves every shade on: now, or with
      // Hold on once the run is resumed and the marker is back.
      step.settled = StaticSettled{flips_, flipOffset(flips_), settleSlots_};
      settleSlots_ = 0;
      if (holdOn_) {
        flipPending_ = true;
      } else {
        flip();
        step.reapply = true;
      }
      return HoldStart{HoldSpot::StaticSettle, step.settled->flip};
    case SweepMode::Slow:
      if (slowMeasured_) return crossingSpot(step, elapsedMs);
      if (driven > 0) {
        // Still painting: the longest delivered slot is the period the crossing must hold (a two-scan slot, as the
        // rectangles' will be, outlasts a one-scan one).
        if (driveDone && periodUs > slowPeriodUs_) slowPeriodUs_ = periodUs;
        return std::nullopt;
      }
      // The paint has settled: size the crossing and start it from here.
      if (slowPeriodUs_ == 0) slowPeriodUs_ = periodUs;
      slowMeasured_ = true;
      slowStartMs_ = elapsedMs;
      sizeSlow(slowPeriodUs_);
      step.measured = true;
      return HoldStart{HoldSpot::SlowPainted, 0};
    case SweepMode::Fast:
      return crossingSpot(step, elapsedMs);
  }
  return std::nullopt;
}

TransitionScene::CrossingTime TransitionScene::crossingAt(const uint32_t elapsedMs) const {
  const uint32_t since =
      mode_ == SweepMode::Slow ? (elapsedMs > slowStartMs_ ? elapsedMs - slowStartMs_ : 0) : elapsedMs;
  const uint32_t length = sweepMs_ + PAUSE_MS;
  return CrossingTime{since / length, since % length};
}

std::optional<HoldStart> TransitionScene::crossingSpot(SceneStep& step, const uint32_t elapsedMs) {
  const bool slow = mode_ == SweepMode::Slow;
  const HoldSpot pauseSpot = slow ? HoldSpot::SlowCrossed : HoldSpot::FastPause;
  const uint32_t cycle = crossingAt(elapsedMs).cycle;
  if (cycle != cycle_) {
    // A crossing left without a slot in its pause: its pause spot was outrun.
    if (!crossPassed_ && holdOn_) step.skipped = SkippedSpot{pauseSpot, cycle_ + 1, SpotSkip::PauseOutrun};
    resetCrossingSpots(cycle);
  }
  const std::optional<int> x = rectX(elapsedMs);
  if (!x) {
    // The first slot of the pause: the rectangles have left the right edge, their trails behind them.
    if (crossPassed_) return std::nullopt;
    crossPassed_ = true;
    return HoldStart{pauseSpot, cycle + 1};
  }
  // Slow: the dwell's slots count from the first slot with the rectangles' left (trailing) edge on-screen.
  if (!slow || dwellPassed_ || *x < 0) return std::nullopt;
  if (++enteredSlots_ < dwellSlots_) return std::nullopt;
  dwellPassed_ = true;
  // Its right edge already past the screen's: no fully on-screen, fully dwelt moment in this crossing.
  if (*x + rectW_ > logicalW_) {
    if (holdOn_) step.skipped = SkippedSpot{HoldSpot::SlowDwelt, cycle + 1, SpotSkip::RightEdge};
    return std::nullopt;
  }
  return HoldStart{HoldSpot::SlowDwelt, cycle + 1};
}

void TransitionScene::flip() {
  ++flips_;
  shift_ = static_cast<uint8_t>((shift_ + flipOffset(flips_)) % SHADE_COUNT);
  settleSlots_ = 0;
}

bool TransitionScene::markerArrived(const GrayModel& model, SceneStep& step) {
  ++markerSlots_;
  if (!markerSettled(model)) return false;
  step.markerSlots = markerSlots_;
  markerSlots_ = 0;
  return true;
}

bool TransitionScene::resumeHold(GrayModel& model, const GrayPreset& preset) {
  if (phase_ != HoldPhase::Held) return false;
  phase_ = HoldPhase::MarkerBack;
  markerOut_ = false;
  markerSlots_ = 0;
  retargetMarker(model, preset);
  return true;
}

std::optional<DroppedHold> TransitionScene::cancelHold() {
  if (phase_ == HoldPhase::Running || !spot_) return std::nullopt;
  HoldDrop drop = HoldDrop::SkippedThisCrossing;
  if (phase_ == HoldPhase::MarkerBack) {
    drop = HoldDrop::WayBackCutOff;
  } else if (*spot_ == HoldSpot::StaticSettle) {
    drop = HoldDrop::StaticAgainAtFlipZero;
  }
  const DroppedHold dropped{*spot_, drop, markerSlots_};
  dropHold();
  return dropped;
}

ScreenRect TransitionScene::markerRect() const {
  if (rectHeight() <= 0) {
    // No lanes, so no rectangle to keep clear of: a square in the corner.
    const int w = logicalW_ < MARKER_PX ? logicalW_ : MARKER_PX;
    const int h = logicalH_ < MARKER_PX ? logicalH_ : MARKER_PX;
    return ScreenRect{logicalW_ - w, logicalH_ - h, w, h};
  }
  // A dash in the bottom band's margin below its last lane's rectangle (its inset and the lanes' leftover rows).
  const int margin = logicalH_ - (rectTop(BANDS - 1, LANES - 1) + rectHeight());
  const int w = logicalW_ < MARKER_W ? logicalW_ : MARKER_W;
  const int h = margin < MARKER_PX ? margin : MARKER_PX;
  return ScreenRect{logicalW_ - w, logicalH_ - h, w, h};
}

Shade TransitionScene::markerShade() const {
  // bandOf puts the last rows in the last band.
  const Shade band = markerBandShade();
  return markerOut_ ? shifted(band, 2) : band;
}

bool TransitionScene::markerSettled(const GrayModel& model) const {
  const ScreenRect logical = markerRect();
  if (logical.w <= 0 || logical.h <= 0) return true;
  const ScreenRect panel = toPanel(turn_, logical, panelW_, panelH_);
  for (int y = panel.y; y < panel.y + panel.h; ++y) {
    for (int x = panel.x; x < panel.x + panel.w; ++x) {
      if (model.remaining(x, y) > 0) return false;
    }
  }
  return true;
}

void TransitionScene::retargetMarker(GrayModel& model, const GrayPreset& preset) const {
  const ScreenRect m = markerRect();
  retargetLogical(model, m.x, m.x + m.w, m.y, m.h, markerShade(), preset);
}

void TransitionScene::restartFlips() {
  flips_ = 0;
  shift_ = 0;
  settleSlots_ = 0;
  flipPending_ = false;
}

}  // namespace GameDisplayLab

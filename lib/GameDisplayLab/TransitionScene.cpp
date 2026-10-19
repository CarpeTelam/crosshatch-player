#include "TransitionScene.h"

namespace GameDisplayLab {

SweepMode nextMode(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return SweepMode::Slow;
    case SweepMode::Slow:
      return SweepMode::Fast;
    case SweepMode::Fast:
      return SweepMode::Ladder;
    case SweepMode::Ladder:
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
    case HoldSpot::FastPainted:
      return "fast painted";
    case HoldSpot::Ladder:
      return "ladder";
  }
  return "";
}

char shadeName(const Shade shade) {
  // D14's seven-level numbers: the four shades sit on the even levels, so 1, 3 and 5 slot in without renaming.
  switch (shade) {
    case Shade::K:
      return '0';
    case Shade::D:
      return '2';
    case Shade::L:
      return '4';
    case Shade::W:
      return '6';
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
    case HoldDrop::LadderFromBase:
      return "the ladder restarts from the base";
  }
  return "";
}

TransitionScene::TransitionScene(const PanelTurn turn, const int logicalW, const int logicalH, const int panelW,
                                 const int panelH)
    : turn_(turn), logicalW_(logicalW), logicalH_(logicalH), panelW_(panelW), panelH_(panelH) {}

void TransitionScene::setMode(const SweepMode mode, const uint32_t dwellSlots) {
  mode_ = mode;
  restartFlips();
  restartLadder();
  dwellSlots_ = dwellSlots > 0 ? dwellSlots : 1;
  painted_ = false;
  paintSlots_ = 0;
  slowPeriodUs_ = 0;
  crossStartMs_ = 0;
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
    case SweepMode::Ladder:
      rectW_ = 0;    // no rectangles (the columns are ladderLayout's)
      sweepMs_ = 0;  // nothing crosses
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
  ladderPending_ = false;
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
      // Slow and Fast wait just off the left edge until the bands' paint has settled, then cross from that slot's
      // clock.
      if (!painted_) return -rectW_;
      const uint32_t t = crossingAt(elapsedMs).ms;
      if (t >= sweepMs_) return std::nullopt;
      const int64_t travel = static_cast<int64_t>(logicalW_) + rectW_;
      return static_cast<int>(-rectW_ + travel * t / sweepMs_);
    }
    case SweepMode::Ladder:
      return std::nullopt;
  }
  return std::nullopt;
}

int TransitionScene::bandOf(const int ly) const {
  const int band = bandHeight() > 0 ? ly / bandHeight() : BANDS - 1;
  return band < BANDS ? band : BANDS - 1;
}

Shade TransitionScene::targetAt(const int lx, const int ly, const uint32_t elapsedMs) const {
  if (markerOut_) {
    // The pause symbol's two bars; the gap between them keeps the band's shade.
    for (const ScreenRect& bar : markerBars()) {
      if (lx >= bar.x && lx < bar.x + bar.w && ly >= bar.y && ly < bar.y + bar.h) return markerShade();
    }
  }
  // The Ladder's base: the top half white, the bottom half and the marker's rows below it black.
  if (mode_ == SweepMode::Ladder) return ly < ladderHeight() / 2 ? Shade::W : Shade::K;
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
      toPanelPixel(turn_, lx, ly, panelW_, panelH_, px, py);
      model.retarget(px, py, targetAt(lx, ly, elapsedMs), preset);
    }
  }
}

void TransitionScene::retargetLogical(GrayModel& model, const int x0, const int x1, const int y, const int h,
                                      const Shade shade, const GrayPreset& preset) const {
  retargetLogicalRect(model, screen(), x0, x1, y, h, shade, preset);
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
      if (ladderPending_) {
        // Ladder: the stage (or the end) the settle held back starts now, the marker back on black.
        ladderPending_ = false;
        advanceLadder(model, step);
      }
      return step;
    case HoldPhase::Running:
      break;
  }
  const std::optional<HoldStart> spot = patternStep(model, step, driven, driveDone, periodUs, elapsedMs);
  if (!spot || !holdsAtSpots()) return step;
  // A photo spot: the marker out to its hold shade, driven by the next slots at a still clock.
  phase_ = HoldPhase::MarkerOut;
  markerOut_ = true;
  spot_ = spot->spot;
  markerSlots_ = 0;
  retargetMarker(model, preset);
  step.hold = spot;
  return step;
}

std::optional<HoldStart> TransitionScene::patternStep(GrayModel& model, SceneStep& step, const size_t driven,
                                                      const bool driveDone, const uint32_t periodUs,
                                                      const uint32_t elapsedMs) {
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
      if (painted_) return crossingSpot(step, elapsedMs);
      if (driven > 0) {
        // Still painting: the longest delivered slot is the period the crossing must hold (a two-scan slot, as the
        // rectangles' will be, outlasts a one-scan one).
        if (driveDone && periodUs > slowPeriodUs_) slowPeriodUs_ = periodUs;
        return std::nullopt;
      }
      // The paint has settled: size the crossing and start it from here.
      if (slowPeriodUs_ == 0) slowPeriodUs_ = periodUs;
      painted_ = true;
      crossStartMs_ = elapsedMs;
      sizeSlow(slowPeriodUs_);
      step.measured = true;
      return HoldStart{HoldSpot::SlowPainted, 0};
    case SweepMode::Fast:
      if (painted_) return crossingSpot(step, elapsedMs);
      if (driven > 0) {
        // Still painting: the rectangles wait off-screen, so none lands on a band still filling from white.
        ++paintSlots_;
        return std::nullopt;
      }
      // The paint has settled: the sweep starts from here (from the resume, with a hold: the clock stands still).
      painted_ = true;
      crossStartMs_ = elapsedMs;
      step.fastPainted = paintSlots_;
      return HoldStart{HoldSpot::FastPainted, 0};
    case SweepMode::Ladder:
      return ladderStep(model, step, driven);
  }
  return std::nullopt;
}

std::optional<HoldStart> TransitionScene::ladderStep(GrayModel& model, SceneStep& step, const size_t driven) {
  switch (ladderStage_) {
    case LadderStage::Base:
      if (driven > 0) {
        ++ladderSlots_;
        return std::nullopt;
      }
      // No pixel had a dose left for this slot: the base settled. It is no photo: stage 1 starts at once.
      step.ladderSettled = LadderSettled{ladderStage_, ladderSlots_};
      advanceLadder(model, step);
      return std::nullopt;
    case LadderStage::Ladder:
    case LadderStage::Return:
      break;
    case LadderStage::Done:
      return std::nullopt;  // the task ended the run at `finished`
  }
  if (ladderSlots_ < static_cast<uint32_t>(LADDER_STEPS)) {
    // A stage slot ran. End-aligned: column n joins before the stage's slot LADDER_STEPS - n, so its n pulses are the
    // stage's last n slots.
    ++ladderSlots_;
    if (ladderSlots_ < static_cast<uint32_t>(LADDER_STEPS)) {
      driveLadderColumn(model, LADDER_STEPS - static_cast<int>(ladderSlots_));
    }
    return std::nullopt;
  }
  // Every column has had its last pulse; the first slot after them that drove none is the stage's end.
  if (driven > 0) return std::nullopt;
  step.ladderSettled = LadderSettled{ladderStage_, ladderSlots_};
  // A photo spot, which always holds (holdsAtSpots): the next stage (or the end) once the marker is back.
  ladderPending_ = true;
  return HoldStart{HoldSpot::Ladder, ladderStage_ == LadderStage::Ladder ? 1u : 2u};
}

void TransitionScene::advanceLadder(GrayModel& model, SceneStep& step) {
  switch (ladderStage_) {
    case LadderStage::Base:
      ladderStage_ = LadderStage::Ladder;
      break;
    case LadderStage::Ladder:
      ladderStage_ = LadderStage::Return;
      break;
    case LadderStage::Return:
      ladderStage_ = LadderStage::Done;
      step.finished = true;
      return;
    case LadderStage::Done:
      return;  // nothing after it
  }
  // A new stage: no slot of it run yet, the longest column (all its slots) driven before the first.
  ladderSlots_ = 0;
  driveLadderColumn(model, LADDER_STEPS);
}

void TransitionScene::driveLadderColumn(GrayModel& model, const int n) const {
  const bool towardOrigin = ladderStage_ == LadderStage::Return;
  for (const LadderHalf half : {LadderHalf::FromWhite, LadderHalf::FromBlack}) {
    // Stage 1 drives each half toward the opposite of its origin, stage 2 back toward it.
    const bool fromWhite = half == LadderHalf::FromWhite;
    const Toward toward = fromWhite == towardOrigin ? Toward::Light : Toward::Dark;
    const ScreenRect r = ladderColumn(half, n);
    if (r.w <= 0 || r.h <= 0) continue;
    const ScreenRect panel = toPanel(turn_, r, panelW_, panelH_);
    model.driveRect(panel.x, panel.y, panel.w, panel.h, toward, static_cast<uint8_t>(n));
  }
}

LadderLayout TransitionScene::ladderLayout(const int logicalW) {
  if (logicalW <= 0) return LadderLayout{0, 0, 0};
  // Under one pixel a column: as many 1 px columns as fit.
  if (logicalW < LADDER_STEPS) return LadderLayout{logicalW, 1, 0};
  const int pitch = logicalW / LADDER_STEPS;
  if (pitch < 2) return LadderLayout{LADDER_STEPS, 1, 0};  // no room for a gutter
  // A 1 px gutter of the half's origin after every column: countable columns, and an untouched reference beside each.
  return LadderLayout{LADDER_STEPS, pitch - 1, 1};
}

ScreenRect TransitionScene::ladderColumn(const LadderHalf half, const int n) const {
  const LadderLayout layout = ladderLayout(logicalW_);
  if (n < 1 || n > layout.columns) return ScreenRect{0, 0, 0, 0};
  const int rows = ladderHeight() > 0 ? ladderHeight() : 0;
  const int top = half == LadderHalf::FromWhite ? 0 : rows / 2;
  const int h = half == LadderHalf::FromWhite ? rows / 2 : rows - rows / 2;
  return ScreenRect{(n - 1) * (layout.width + layout.gutter), top, layout.width, h};
}

void TransitionScene::restartLadder() {
  ladderStage_ = LadderStage::Base;
  ladderSlots_ = 0;
  ladderPending_ = false;
}

GrayPreset TransitionScene::heldRhythm(const GrayPreset& preset) {
  GrayPreset held = preset;
  held.holdMask = 0xFFFF;  // every source > target bit: no follow-up scan for any pixel
  return held;
}

const GrayPreset& TransitionScene::slotPreset(const GrayPreset& preset) {
  if (!ladderPulsing()) return preset;
  held_ = heldRhythm(preset);
  return held_;
}

bool TransitionScene::ladderCleaned(const std::optional<DroppedHold>& dropped) {
  // The way back from `ladder 2` cut off: both photos were taken, and the return's end is all that was left.
  if (dropped && dropped->spot == HoldSpot::Ladder && dropped->drop == HoldDrop::WayBackCutOff &&
      ladderStage_ == LadderStage::Return) {
    ladderStage_ = LadderStage::Done;
    return true;
  }
  restartLadder();
  return false;
}

TransitionScene::CrossingTime TransitionScene::crossingAt(const uint32_t elapsedMs) const {
  const uint32_t since = elapsedMs > crossStartMs_ ? elapsedMs - crossStartMs_ : 0;
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

namespace {

// The drop of a hold at `spot` whose marker is going out or held (not on its way back).
HoldDrop dropBeforeTheWayBack(const HoldSpot spot) {
  switch (spot) {
    case HoldSpot::StaticSettle:
      return HoldDrop::StaticAgainAtFlipZero;
    case HoldSpot::Ladder:
      return HoldDrop::LadderFromBase;
    case HoldSpot::SlowPainted:
    case HoldSpot::SlowDwelt:
    case HoldSpot::SlowCrossed:
    case HoldSpot::FastPainted:
    case HoldSpot::FastPause:
      return HoldDrop::SkippedThisCrossing;
  }
  return HoldDrop::SkippedThisCrossing;
}

}  // namespace

std::optional<DroppedHold> TransitionScene::cancelHold() {
  if (phase_ == HoldPhase::Running || !spot_) return std::nullopt;
  const HoldDrop drop = phase_ == HoldPhase::MarkerBack ? HoldDrop::WayBackCutOff : dropBeforeTheWayBack(*spot_);
  const DroppedHold dropped{*spot_, drop, markerSlots_};
  dropHold();
  return dropped;
}

ScreenRect TransitionScene::markerRect() const {
  if (mode_ == SweepMode::Ladder) {
    // No lanes to keep clear of: a full-height dash, the ladder's columns ending above its rows.
    const int w = logicalW_ < MARKER_W ? logicalW_ : MARKER_W;
    const int h = logicalH_ < MARKER_PX ? logicalH_ : MARKER_PX;
    return ScreenRect{logicalW_ - w, logicalH_ - h, w, h};
  }
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

std::array<ScreenRect, 2> TransitionScene::markerBars() const { return pauseBars(markerRect(), MARKER_GAP); }

bool TransitionScene::markerSettled(const GrayModel& model) const { return barsSettled(model, markerBars(), screen()); }

void TransitionScene::retargetMarker(GrayModel& model, const GrayPreset& preset) const {
  for (const ScreenRect& bar : markerBars()) {
    retargetLogical(model, bar.x, bar.x + bar.w, bar.y, bar.h, markerShade(), preset);
  }
}

void TransitionScene::restartFlips() {
  flips_ = 0;
  shift_ = 0;
  settleSlots_ = 0;
  flipPending_ = false;
}

}  // namespace GameDisplayLab

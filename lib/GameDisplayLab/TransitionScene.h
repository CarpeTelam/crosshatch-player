#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "GrayModel.h"
#include "GrayPreset.h"

// The transition test's picture (the display-lab epic, R4; InkBoy's tuning.md, digest Q6; story dlab-c3's modes),
// pure and pinned by test_transition_scene.cpp. Over the logical screen (what the renderer's orientation shows the
// user): four shade bands K, D, L, W top to bottom, each a quarter of the height; each band has three lanes (a third of
// the band each) holding a rectangle of one of the other three shades, in K, D, L, W order, inset RECT_INSET px top and
// bottom in its lane. Pixels below the fourth band (a height not divisible by four) belong to the last band, and lane
// leftovers to the band's shade. The mode (setMode) sets the rectangles' width and motion:
// - Fast: RECT_W px wide, sweeping from just off the left edge (-width) to the right edge over SWEEP_MS, linearly,
//   then hidden for PAUSE_MS, and the cycle repeats (the SSD1677's sweep, dlab-c1).
// - Slow: RECT_W_SLOW px wide, the same sweep and pause over a crossing time sized from the slot period and the
//   preset's longest dose run in slots (slowSweepMs, GrayPreset::longestRunSlots), so a pixel stays under a rectangle
//   until its transition can finish. The rectangles wait off-screen until the run's first delivered slot has measured
//   the period, then start their crossing (slotDone).
// - Static: a third of the width wide, centred, never moving. The pattern settles, then flips: every target moves on by
//   the flip's offset, +1, +2, +3, +1, ... (flipOffset), so three flips take every pixel through all twelve
//   transitions between them (slotDone).
//
// The model is in panel coordinates; toPanel maps a logical rectangle the way GfxRenderer::rotateCoordinates maps a
// logical pixel for each orientation.
namespace GameDisplayLab {

// The four GfxRenderer orientations, in its enum's order.
enum class PanelTurn : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

// The transition test's modes, in the Mode row's order.
enum class SweepMode : uint8_t { Static, Slow, Fast };

// The Mode row's next mode: Static, Slow, Fast, then Static again.
SweepMode nextMode(SweepMode mode);

struct ScreenRect {
  int x;
  int y;
  int w;
  int h;
};

// The panel rectangle that logical rectangle `r` (w, h > 0) covers under `turn`, on a panel panelW x panelH.
ScreenRect toPanel(PanelTurn turn, const ScreenRect& r, int panelW, int panelH);

// A Static pattern that settled: its flip number (0 is the first paint), the offset that flip moved every shade by (0
// for the first paint), and the slots that drove pixels until it settled.
struct StaticSettled {
  uint32_t flip;
  uint8_t offset;
  uint32_t slots;
};

// What a slot left for the scene to do (TransitionScene::slotDone).
struct SceneStep {
  // Retarget every pixel to the scene (applyAll) before the next slot: a Static flip.
  bool reapply = false;
  // Static: the pattern that settled in this slot.
  std::optional<StaticSettled> settled;
  // Slow: this slot measured the period, the crossing is sized from it and starts now.
  bool measured = false;
};

class TransitionScene {
 public:
  // Fast: today's sweep.
  static constexpr uint32_t SWEEP_MS = 5000;
  static constexpr int RECT_W = 12;
  // Fast and Slow: the rectangles are hidden this long between two crossings.
  static constexpr uint32_t PAUSE_MS = 1000;
  // Slow: the rectangles' width and the longest crossing.
  static constexpr int RECT_W_SLOW = 48;
  static constexpr uint32_t SLOW_SWEEP_MAX_MS = 120000;
  static constexpr int RECT_INSET = 8;
  static constexpr int BANDS = 4;
  static constexpr int LANES = 3;

  TransitionScene() = default;
  // A logical screen logicalW x logicalH shown on a panel panelW x panelH (the model's size) under `turn`; Fast.
  TransitionScene(PanelTurn turn, int logicalW, int logicalH, int panelW, int panelH);

  // The mode's geometry. Slow sizes its crossing from `slotPeriodUs` and `dwellSlots` (the preset's
  // longestRunSlots(); slowSweepMs), which the other modes ignore. Restarts Static's flips and Slow's measurement.
  void setMode(SweepMode mode, uint32_t slotPeriodUs, uint32_t dwellSlots);
  SweepMode mode() const { return mode_; }
  int rectW() const { return rectW_; }
  // The crossing time (Fast and Slow; Static's rectangles do not move).
  uint32_t sweepMs() const { return sweepMs_; }
  // Slow's crossing time for a slot period on a logical width `logicalW`: the rectangle covers a pixel for at least
  // `dwellSlots` periods, rounded up to a millisecond, at most SLOW_SWEEP_MAX_MS.
  static uint32_t slowSweepMs(uint32_t slotPeriodUs, int logicalW, uint32_t dwellSlots);
  // Whole slots of `slotPeriodUs` a pixel stays under a rectangle at the current crossing (Fast and Slow).
  uint32_t dwellSlotsAt(uint32_t slotPeriodUs) const;
  // Slow's crossing was cut to SLOW_SWEEP_MAX_MS.
  bool sweepCapped() const { return sweepCapped_; }

  // The rectangles' left edge at `elapsedMs` (all twelve share it); nothing during the pause. Slow holds them just off
  // the left edge until a slot has measured the period, and starts the crossing at that slot's clock.
  std::optional<int> rectX(uint32_t elapsedMs) const;
  // The shade logical pixel (lx, ly) shows at `elapsedMs`.
  Shade targetAt(int lx, int ly, uint32_t elapsedMs) const;
  // Retargets every model pixel to the scene at `elapsedMs` (each pixel once).
  void applyAll(GrayModel& model, uint32_t elapsedMs, const GrayPreset& preset) const;
  // Moves the rectangles from `prevMs` to `nowMs`: each new rectangle's area to its shade, the old area it no longer
  // covers back to its band's shade; no other pixel is touched. Nothing when they did not move (always in Static).
  void update(GrayModel& model, uint32_t prevMs, uint32_t nowMs, const GrayPreset& preset) const;

  // After each slot that ran at the sweep clock `elapsedMs`: it drove `driven` pixels, its drive scan was `driveDone`,
  // and the slot took `periodUs` (its measured period). Static: a slot that drove none means the pattern settled; the
  // scene moves on to the next flip and asks for a reapply. Slow: the first slot since setMode whose drive scan was
  // delivered sizes the crossing from `periodUs` and starts it at `elapsedMs` (the rectangles were off-screen, so
  // nothing to reapply). Fast: nothing.
  SceneStep slotDone(size_t driven, bool driveDone, uint32_t periodUs, uint32_t elapsedMs);
  // The offset flip `flip` moves every shade by: 0 for the first paint, then +1, +2, +3, +1, ...
  static uint8_t flipOffset(uint32_t flip);
  // Static: the sum of the flips' offsets so far (mod 4), which every shade shows moved by.
  uint8_t shift() const { return shift_; }
  // Back to flip 0, unshifted (a replay, or the session cap's clean back to white).
  void restartFlips();

  static Shade bandShade(int band) { return static_cast<Shade>(band); }
  // The shade of lane `lane` (0..2) in band `band`: the other shades in K, D, L, W order.
  static Shade laneShade(int band, int lane) { return static_cast<Shade>(lane < band ? lane : lane + 1); }
  // `shade` moved on by `offset` steps, K, D, L, W, K, ...
  static Shade shifted(Shade shade, uint8_t offset) {
    return static_cast<Shade>((static_cast<uint8_t>(shade) + offset) % SHADE_COUNT);
  }
  int bandHeight() const { return logicalH_ / BANDS; }
  int laneHeight() const { return bandHeight() / LANES; }
  // The logical top and height of lane `lane`'s rectangle in band `band`.
  int rectTop(int band, int lane) const { return band * bandHeight() + lane * laneHeight() + RECT_INSET; }
  int rectHeight() const { return laneHeight() - 2 * RECT_INSET; }

 private:
  // Retargets the panel pixels of logical rectangle [x0, x1) x [y, y + h), clipped to the logical screen.
  void retargetLogical(GrayModel& model, int x0, int x1, int y, int h, Shade shade, const GrayPreset& preset) const;
  int bandOf(int ly) const;
  // Slow's crossing for `slotPeriodUs` and the dwell, and whether the cap cut it.
  void sizeSlow(uint32_t slotPeriodUs);
  // `shade` as the pattern shows it now (moved by the flips' shift).
  Shade shown(Shade shade) const { return shifted(shade, shift_); }

  PanelTurn turn_ = PanelTurn::LandscapeCounterClockwise;
  int logicalW_ = 0;
  int logicalH_ = 0;
  int panelW_ = 0;
  int panelH_ = 0;
  SweepMode mode_ = SweepMode::Fast;
  int rectW_ = RECT_W;
  uint32_t sweepMs_ = SWEEP_MS;
  bool sweepCapped_ = false;
  // Static: flips so far, the shift they add up to, and the slots that drove pixels since the last one.
  uint32_t flips_ = 0;
  uint8_t shift_ = 0;
  uint32_t settleSlots_ = 0;
  // Slow: the dwell the crossing is sized for, whether a slot has measured the period since setMode, and the sweep
  // clock at which the crossing started.
  uint32_t dwellSlots_ = 1;
  bool slowMeasured_ = false;
  uint32_t slowStartMs_ = 0;
};

}  // namespace GameDisplayLab

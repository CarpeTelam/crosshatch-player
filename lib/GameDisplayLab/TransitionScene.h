#pragma once

#include <cstdint>
#include <optional>

#include "GrayModel.h"
#include "GrayPreset.h"

// The transition test's picture (the display-lab epic, R4; InkBoy's tuning.md, digest Q6), pure and pinned by
// test_transition_scene.cpp. Over the logical screen (what the renderer's orientation shows the user): four shade
// bands K, D, L, W top to bottom, each a quarter of the height; each band has three lanes (a third of the band each)
// holding a RECT_W-px-wide rectangle of one of the other three shades, in K, D, L, W order, inset RECT_INSET px top
// and bottom in its lane. The rectangles sweep from just off the left edge (-RECT_W) to the right edge over SWEEP_MS,
// linearly, then are hidden for PAUSE_MS, and the cycle repeats. Pixels below the fourth band (a height not divisible
// by four) belong to the last band, and lane leftovers to the band's shade.
//
// The model is in panel coordinates; toPanel maps a logical rectangle the way GfxRenderer::rotateCoordinates maps a
// logical pixel for each orientation.
namespace GameDisplayLab {

// The four GfxRenderer orientations, in its enum's order.
enum class PanelTurn : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

struct ScreenRect {
  int x;
  int y;
  int w;
  int h;
};

// The panel rectangle that logical rectangle `r` (w, h > 0) covers under `turn`, on a panel panelW x panelH.
ScreenRect toPanel(PanelTurn turn, const ScreenRect& r, int panelW, int panelH);

class TransitionScene {
 public:
  static constexpr uint32_t SWEEP_MS = 5000;
  static constexpr uint32_t PAUSE_MS = 1000;
  static constexpr uint32_t CYCLE_MS = SWEEP_MS + PAUSE_MS;
  static constexpr int RECT_W = 12;
  static constexpr int RECT_INSET = 8;
  static constexpr int BANDS = 4;
  static constexpr int LANES = 3;

  TransitionScene() = default;
  // A logical screen logicalW x logicalH shown on a panel panelW x panelH (the model's size) under `turn`.
  TransitionScene(PanelTurn turn, int logicalW, int logicalH, int panelW, int panelH);

  // The rectangles' left edge at `elapsedMs` (all twelve share it); nothing during the pause.
  static std::optional<int> rectX(uint32_t elapsedMs, int logicalW);
  // The shade logical pixel (lx, ly) shows at `elapsedMs`.
  Shade targetAt(int lx, int ly, uint32_t elapsedMs) const;
  // Retargets every model pixel to the scene at `elapsedMs` (each pixel once).
  void applyAll(GrayModel& model, uint32_t elapsedMs, const GrayPreset& preset) const;
  // Moves the rectangles from `prevMs` to `nowMs`: each new rectangle's area to its shade, the old area it no longer
  // covers back to its band's shade; no other pixel is touched.
  void update(GrayModel& model, uint32_t prevMs, uint32_t nowMs, const GrayPreset& preset) const;

  static Shade bandShade(int band) { return static_cast<Shade>(band); }
  // The shade of lane `lane` (0..2) in band `band`: the other shades in K, D, L, W order.
  static Shade laneShade(int band, int lane) { return static_cast<Shade>(lane < band ? lane : lane + 1); }
  int bandHeight() const { return logicalH_ / BANDS; }
  int laneHeight() const { return bandHeight() / LANES; }
  // The logical top and height of lane `lane`'s rectangle in band `band`.
  int rectTop(int band, int lane) const { return band * bandHeight() + lane * laneHeight() + RECT_INSET; }
  int rectHeight() const { return laneHeight() - 2 * RECT_INSET; }

 private:
  // Retargets the panel pixels of logical rectangle [x0, x1) x [y, y + h), clipped to the logical screen.
  void retargetLogical(GrayModel& model, int x0, int x1, int y, int h, Shade shade, const GrayPreset& preset) const;
  int bandOf(int ly) const;

  PanelTurn turn_ = PanelTurn::LandscapeCounterClockwise;
  int logicalW_ = 0;
  int logicalH_ = 0;
  int panelW_ = 0;
  int panelH_ = 0;
};

}  // namespace GameDisplayLab

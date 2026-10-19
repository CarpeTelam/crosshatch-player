#pragma once

#include <array>
#include <cstdint>

#include "GrayModel.h"
#include "GrayPreset.h"

// The lab's logical-screen geometry over the panel model, shared by the transition test's scene (TransitionScene) and
// the gray viewer's picture (GrayPicture): the four renderer orientations, a logical rectangle mapped to the panel the
// way GfxRenderer::rotateCoordinates maps a pixel, a logical rectangle retargeted in the model, and the hold marker's
// pause symbol (story dlab-c4, the epic's D13 and D15: two bars inside a box, the gap between them never driven).
// Pure C++, pinned by test_transition_scene.cpp (toPanel against a copy of the renderer's map) and
// test_gray_picture.cpp.
namespace GameDisplayLab {

// The four GfxRenderer orientations, in its enum's order.
enum class PanelTurn : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

struct ScreenRect {
  int x;
  int y;
  int w;
  int h;
};

// GfxRenderer::rotateCoordinates for one pixel: logical (x, y) to panel (px, py) under `turn`.
void toPanelPixel(PanelTurn turn, int x, int y, int panelW, int panelH, int& px, int& py);

// The panel rectangle that logical rectangle `r` (w, h > 0) covers under `turn`, on a panel panelW x panelH.
ScreenRect toPanel(PanelTurn turn, const ScreenRect& r, int panelW, int panelH);

// A logical screen logicalW x logicalH shown on a panel panelW x panelH (the model's size) under `turn`.
struct LogicalScreen {
  PanelTurn turn;
  int logicalW;
  int logicalH;
  int panelW;
  int panelH;
};

// Retargets the panel pixels of logical rectangle [x0, x1) x [y, y + h) to `shade`, clipped to the logical screen;
// nothing when the clipped rectangle is empty.
void retargetLogicalRect(GrayModel& model, const LogicalScreen& screen, int x0, int x1, int y, int h, Shade shade,
                         const GrayPreset& preset);

// The pause symbol's two bars inside logical box `box`: each (w - gap) / 2 wide at its left and right edge, full
// height, the gap between them never driven; on a box too narrow for both and the gap, the whole box and an empty
// second bar (w 0, just right of the box).
std::array<ScreenRect, 2> pauseBars(const ScreenRect& box, int gap);

// No pixel of logical bars `bars` (an empty one skipped) has a dose left in `model`.
bool barsSettled(const GrayModel& model, const std::array<ScreenRect, 2>& bars, const LogicalScreen& screen);

}  // namespace GameDisplayLab

// The transition test's picture (lib/GameDisplayLab/TransitionScene): toPanel against a copy of
// GfxRenderer::rotateCoordinates for all four orientations, the bands and lanes, the sweep timing, and update()
// touching only the rectangles' old and new areas (the plan's Transition sweep row, its pure part).

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "GrayModel.h"
#include "GrayPreset.h"
#include "TransitionScene.h"

namespace {

using namespace GameDisplayLab;

// A copy of GfxRenderer.cpp's rotateCoordinates (lib/GfxRenderer), the map toPanel must equal.
void rendererRotate(const PanelTurn turn, const int x, const int y, int* phyX, int* phyY, const uint16_t panelWidth,
                    const uint16_t panelHeight) {
  switch (turn) {
    case PanelTurn::Portrait: {
      *phyX = y;
      *phyY = panelHeight - 1 - x;
      break;
    }
    case PanelTurn::LandscapeClockwise: {
      *phyX = panelWidth - 1 - x;
      *phyY = panelHeight - 1 - y;
      break;
    }
    case PanelTurn::PortraitInverted: {
      *phyX = panelWidth - 1 - y;
      *phyY = x;
      break;
    }
    case PanelTurn::LandscapeCounterClockwise: {
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

constexpr PanelTurn TURNS[] = {PanelTurn::Portrait, PanelTurn::LandscapeClockwise, PanelTurn::PortraitInverted,
                               PanelTurn::LandscapeCounterClockwise};

bool portrait(const PanelTurn turn) { return turn == PanelTurn::Portrait || turn == PanelTurn::PortraitInverted; }

TEST(TransitionScene, ToPanelEqualsTheRenderersRotation) {
  constexpr int PW = 40;
  constexpr int PH = 24;
  for (const PanelTurn turn : TURNS) {
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    const ScreenRect rects[] = {{0, 0, 1, 1}, {3, 5, 7, 2}, {0, 0, lw, lh}, {lw - 4, lh - 3, 4, 3}};
    for (const ScreenRect& r : rects) {
      const ScreenRect panel = toPanel(turn, r, PW, PH);
      EXPECT_EQ(panel.w * panel.h, r.w * r.h) << int(turn);
      for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
          int px = 0;
          int py = 0;
          rendererRotate(turn, x, y, &px, &py, PW, PH);
          EXPECT_TRUE(px >= panel.x && px < panel.x + panel.w && py >= panel.y && py < panel.y + panel.h)
              << "turn " << int(turn) << " pixel " << x << "," << y;
        }
      }
    }
  }
}

TEST(TransitionScene, RectangleSweepsThenPauses) {
  constexpr int LW = 800;
  EXPECT_EQ(TransitionScene::rectX(0, LW), -12);
  EXPECT_EQ(TransitionScene::rectX(2500, LW), -12 + 812 / 2);
  EXPECT_EQ(TransitionScene::rectX(4999, LW), -12 + 812 * 4999 / 5000);
  EXPECT_EQ(TransitionScene::rectX(5000, LW), std::nullopt);
  EXPECT_EQ(TransitionScene::rectX(5999, LW), std::nullopt);
  EXPECT_EQ(TransitionScene::rectX(6000, LW), -12);
  EXPECT_EQ(TransitionScene::rectX(6000 + 2500, LW), -12 + 812 / 2);
}

TEST(TransitionScene, BandsAndLanesHoldTheOtherShadesInOrder) {
  // 800 x 480: bands of 120, lanes of 40, rectangles 24 high from 8 px into each lane.
  const TransitionScene scene(PanelTurn::LandscapeCounterClockwise, 800, 480, 800, 480);
  EXPECT_EQ(scene.bandHeight(), 120);
  EXPECT_EQ(scene.laneHeight(), 40);
  EXPECT_EQ(scene.rectHeight(), 24);
  const Shade expected[4][3] = {{Shade::D, Shade::L, Shade::W},
                                {Shade::K, Shade::L, Shade::W},
                                {Shade::K, Shade::D, Shade::W},
                                {Shade::K, Shade::D, Shade::L}};
  const uint32_t ms = 2500;  // x = 394
  for (int band = 0; band < 4; ++band) {
    for (int lane = 0; lane < 3; ++lane) {
      EXPECT_EQ(TransitionScene::laneShade(band, lane), expected[band][lane]);
      const int top = band * 120 + lane * 40;
      EXPECT_EQ(scene.targetAt(394, top + 8, ms), expected[band][lane]);
      EXPECT_EQ(scene.targetAt(405, top + 31, ms), expected[band][lane]);
      EXPECT_EQ(scene.targetAt(394, top + 7, ms), TransitionScene::bandShade(band));   // inset
      EXPECT_EQ(scene.targetAt(394, top + 32, ms), TransitionScene::bandShade(band));  // inset
      EXPECT_EQ(scene.targetAt(393, top + 20, ms), TransitionScene::bandShade(band));  // left of it
      EXPECT_EQ(scene.targetAt(406, top + 20, ms), TransitionScene::bandShade(band));  // right of it
    }
  }
  EXPECT_EQ(scene.targetAt(394, 20, 5500), Shade::K);  // paused: bands only
  EXPECT_EQ(scene.targetAt(0, 479, ms), Shade::W);
}

struct SceneBench {
  int pw;
  int ph;
  std::vector<uint8_t> pixels;
  GrayModel model;
  GrayPreset preset = GrayPreset::inkBoyDefaults();

  SceneBench(const int panelW, const int panelH) : pw(panelW), ph(panelH), pixels(GrayModel::bytesFor(panelW, panelH)) {
    model = GrayModel(pixels.data(), static_cast<uint16_t>(pw), static_cast<uint16_t>(ph));
    model.reset(Shade::W);
  }
};

TEST(TransitionScene, ApplyAllRetargetsEveryPixelToTheSceneInEveryTurn) {
  constexpr int PW = 64;
  constexpr int PH = 160;
  for (const PanelTurn turn : TURNS) {
    SceneBench b(PW, PH);
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    const TransitionScene scene(turn, lw, lh, PW, PH);
    scene.applyAll(b.model, 1000, b.preset);
    for (int ly = 0; ly < lh; ++ly) {
      for (int lx = 0; lx < lw; ++lx) {
        int px = 0;
        int py = 0;
        rendererRotate(turn, lx, ly, &px, &py, PW, PH);
        ASSERT_EQ(b.model.target(px, py), static_cast<uint8_t>(scene.targetAt(lx, ly, 1000)))
            << "turn " << int(turn) << " logical " << lx << "," << ly;
      }
    }
  }
}

TEST(TransitionScene, UpdateMovesOnlyTheRectangles) {
  // A logical 96 x 480 screen (bands of 120, lanes of 40, rectangles 24 high), in every turn.
  constexpr int W = 96;
  constexpr int HH = 480;
  for (const PanelTurn turn : TURNS) {
    const int pw = portrait(turn) ? HH : W;
    const int ph = portrait(turn) ? W : HH;
    SceneBench b(pw, ph);
    const TransitionScene scene(turn, W, HH, pw, ph);
    const uint32_t prev = 1000;  // x 9
    const uint32_t now = 1500;   // x 20: the new rectangle overlaps the old one by a pixel
    scene.applyAll(b.model, prev, b.preset);
    // Settled, so any pixel update() touches shows as a changed state.
    while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
    const std::vector<uint8_t> atPrev = b.pixels;
    scene.update(b.model, prev, now, b.preset);
    for (int ly = 0; ly < HH; ++ly) {
      for (int lx = 0; lx < W; ++lx) {
        int px = 0;
        int py = 0;
        rendererRotate(turn, lx, ly, &px, &py, static_cast<uint16_t>(pw), static_cast<uint16_t>(ph));
        const Shade wanted = scene.targetAt(lx, ly, now);
        EXPECT_EQ(b.model.target(px, py), static_cast<uint8_t>(wanted)) << lx << "," << ly;
        const bool changed = scene.targetAt(lx, ly, prev) != wanted;
        const size_t i = 2 * (static_cast<size_t>(py) * pw + px);
        if (!changed) {
          // Untouched: the same bytes as before the update.
          EXPECT_EQ(b.pixels[i], atPrev[i]) << lx << "," << ly;
          EXPECT_EQ(b.pixels[i + 1], atPrev[i + 1]) << lx << "," << ly;
        } else {
          EXPECT_GT(b.model.remaining(px, py), 0) << lx << "," << ly;
        }
      }
    }
  }
}

TEST(TransitionScene, UpdateIntoThePauseClearsTheRectanglesAndBackStartsThem) {
  constexpr int W = 96;
  constexpr int HH = 480;
  SceneBench b(W, HH);
  const TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.applyAll(b.model, 4990, b.preset);
  scene.update(b.model, 4990, 5200, b.preset);
  for (int y = 0; y < HH; ++y) {
    for (int x = 0; x < W; ++x) {
      ASSERT_EQ(b.model.target(x, y), static_cast<uint8_t>(scene.targetAt(x, y, 5200))) << x << "," << y;
    }
  }
  scene.update(b.model, 5200, 6100, b.preset);  // the next sweep has started
  for (int y = 0; y < HH; ++y) {
    for (int x = 0; x < W; ++x) {
      ASSERT_EQ(b.model.target(x, y), static_cast<uint8_t>(scene.targetAt(x, y, 6100))) << x << "," << y;
    }
  }
  // Across a whole cycle in one step (a long stall): the old rectangles are cleared, the new ones drawn.
  scene.update(b.model, 6100, 6100 + 6000 + 3000, b.preset);
  for (int y = 0; y < HH; ++y) {
    for (int x = 0; x < W; ++x) {
      ASSERT_EQ(b.model.target(x, y), static_cast<uint8_t>(scene.targetAt(x, y, 15100))) << x << "," << y;
    }
  }
}

// Every logical pixel of the rectangles' rows: the old rectangle's area not under the new one is back on its band's
// shade, the new rectangle's area has its lane's shade, and everything else is untouched.
void expectRectanglesMoved(const uint32_t prevMs, const uint32_t nowMs) {
  constexpr int W = 96;
  constexpr int HH = 480;
  SceneBench b(W, HH);
  const TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.applyAll(b.model, prevMs, b.preset);
  while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
  const std::vector<uint8_t> atPrev = b.pixels;
  scene.update(b.model, prevMs, nowMs, b.preset);
  const std::optional<int> before = TransitionScene::rectX(prevMs, W);
  const std::optional<int> after = TransitionScene::rectX(nowMs, W);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(after.has_value());
  for (int band = 0; band < TransitionScene::BANDS; ++band) {
    for (int lane = 0; lane < TransitionScene::LANES; ++lane) {
      const int top = scene.rectTop(band, lane);
      for (int y = top; y < top + scene.rectHeight(); ++y) {
        for (int x = 0; x < W; ++x) {
          const bool inNew = x >= *after && x < *after + TransitionScene::RECT_W;
          const bool inOld = x >= *before && x < *before + TransitionScene::RECT_W;
          const size_t i = 2 * (static_cast<size_t>(y) * W + x);
          if (inNew) {
            EXPECT_EQ(b.model.target(x, y), static_cast<uint8_t>(TransitionScene::laneShade(band, lane)))
                << x << "," << y;
          } else if (inOld) {
            EXPECT_EQ(b.model.target(x, y), static_cast<uint8_t>(TransitionScene::bandShade(band))) << x << "," << y;
            EXPECT_GT(b.model.remaining(x, y), 0) << x << "," << y;
          } else {
            EXPECT_EQ(b.pixels[i], atPrev[i]) << x << "," << y;
            EXPECT_EQ(b.pixels[i + 1], atPrev[i + 1]) << x << "," << y;
          }
        }
      }
    }
  }
}

TEST(TransitionScene, UpdateFromTheEndOfOneSweepIntoTheNext) {
  // 4990 ms (x 95, at the right edge) straight to 6050 ms (x -11) with no update during the pause.
  expectRectanglesMoved(4990, 6050);
}

TEST(TransitionScene, UpdateJumpsFurtherThanARectangleWithinOneSweep) {
  // 1000 ms (x 9) to 2000 ms (x 31): 22 px, the old and new rectangles do not overlap.
  expectRectanglesMoved(1000, 2000);
}

}  // namespace

// The transition test's picture (lib/GameDisplayLab/TransitionScene): toPanel against a copy of
// GfxRenderer::rotateCoordinates for all four orientations, the bands and lanes, the sweep timing, update() touching
// only the rectangles' old and new areas (the plan's Transition sweep row, its pure part), and dlab-c3's modes: Slow's
// crossing from the paint's longest slot and the longest dose run, held off-screen until the paint settles, Static's
// fixed rectangles and its settle-and-flip sequence through all twelve transitions, both decided by slotDone; and
// dlab-c4's holds for photos: the hold marker's rectangle and shade in every turn, the photo spots per mode (Static's
// settles, Slow's painted bands, full dwell and crossing end, Fast's pause), the resume sequence that drives the marker
// back before the pattern moves, Hold off, and the session cap's clean dropping a hold; and dlab-g1's Ladder: the
// columns' layout and rectangles in every turn, the base, stage 1's n pulses per column one a slot, the holds, the
// return, the end, Hold off, and the session cap's clean restarting it from the base.

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <utility>
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
  // Fast, the default and today's sweep: 12 px over 5 s, then 1 s hidden.
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, 800, 480, 800, 480);
  EXPECT_EQ(scene.mode(), SweepMode::Fast);
  for (int pass = 0; pass < 2; ++pass) {
    EXPECT_EQ(scene.rectW(), 12);
    EXPECT_EQ(scene.sweepMs(), 5000u);
    EXPECT_EQ(scene.rectX(0), -12);
    EXPECT_EQ(scene.rectX(2500), -12 + 812 / 2);
    EXPECT_EQ(scene.rectX(4999), -12 + 812 * 4999 / 5000);
    EXPECT_EQ(scene.rectX(5000), std::nullopt);
    EXPECT_EQ(scene.rectX(5999), std::nullopt);
    EXPECT_EQ(scene.rectX(6000), -12);
    EXPECT_EQ(scene.rectX(6000 + 2500), -12 + 812 / 2);
    scene.setMode(SweepMode::Fast, 12);  // the dwell changes nothing in Fast
  }
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

// slotDone for a scene that holds nothing (Hold off): no model to drive.
SceneStep slot(TransitionScene& scene, const size_t driven, const bool driveDone, const uint32_t periodUs,
               const uint32_t elapsedMs) {
  GrayModel none;
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  return scene.slotDone(none, preset, driven, driveDone, periodUs, elapsedMs);
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
        const size_t i = GrayModel::PIXEL_BYTES * (static_cast<size_t>(py) * pw + px);
        if (!changed) {
          // Untouched: the same bytes as before the update.
          for (size_t k = 0; k < GrayModel::PIXEL_BYTES; ++k)
            EXPECT_EQ(b.pixels[i + k], atPrev[i + k]) << lx << "," << ly;
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
  const std::optional<int> before = scene.rectX(prevMs);
  const std::optional<int> after = scene.rectX(nowMs);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(after.has_value());
  for (int band = 0; band < TransitionScene::BANDS; ++band) {
    for (int lane = 0; lane < TransitionScene::LANES; ++lane) {
      const int top = scene.rectTop(band, lane);
      for (int y = top; y < top + scene.rectHeight(); ++y) {
        for (int x = 0; x < W; ++x) {
          const bool inNew = x >= *after && x < *after + TransitionScene::RECT_W;
          const bool inOld = x >= *before && x < *before + TransitionScene::RECT_W;
          const size_t i = GrayModel::PIXEL_BYTES * (static_cast<size_t>(y) * W + x);
          if (inNew) {
            EXPECT_EQ(b.model.target(x, y), static_cast<uint8_t>(TransitionScene::laneShade(band, lane)))
                << x << "," << y;
          } else if (inOld) {
            EXPECT_EQ(b.model.target(x, y), static_cast<uint8_t>(TransitionScene::bandShade(band))) << x << "," << y;
            EXPECT_GT(b.model.remaining(x, y), 0) << x << "," << y;
          } else {
            for (size_t k = 0; k < GrayModel::PIXEL_BYTES; ++k)
              EXPECT_EQ(b.pixels[i + k], atPrev[i + k]) << x << "," << y;
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

TEST(TransitionScene, ModeRowCyclesStaticSlowFastLadder) {
  EXPECT_EQ(nextMode(SweepMode::Static), SweepMode::Slow);
  EXPECT_EQ(nextMode(SweepMode::Slow), SweepMode::Fast);
  EXPECT_EQ(nextMode(SweepMode::Fast), SweepMode::Ladder);
  EXPECT_EQ(nextMode(SweepMode::Ladder), SweepMode::Static);
}

// Slow's measurement as a run makes it: one painting slot of `periodUs` whose drive scan was delivered, then a slot
// with nothing to drive at `atMs`, which starts the crossing there.
void paintAndSettle(TransitionScene& scene, const uint32_t periodUs, const uint32_t atMs) {
  ASSERT_FALSE(slot(scene, 1, true, periodUs, atMs).measured);
  ASSERT_TRUE(slot(scene, 0, true, 100000, atMs).measured);
}

TEST(TransitionScene, SlowCrossingKeepsAPixelUnderARectangleForTheLongestDoseRun) {
  // dwell slots x period x (width + 48) / 48, rounded up to a millisecond.
  EXPECT_EQ(TransitionScene::slowSweepMs(100000, 800, 12), 21200u);
  EXPECT_EQ(TransitionScene::slowSweepMs(380000, 800, 12), 80560u);
  EXPECT_EQ(TransitionScene::slowSweepMs(380000, 480, 12), 50160u);  // portrait
  EXPECT_EQ(TransitionScene::slowSweepMs(100000, 800, 7), 12367u);   // 12,366.67 ms, rounded up
  EXPECT_EQ(TransitionScene::slowSweepMs(380000, 800, 7), 46994u);   // the UltraChip preset at the UC8179's slot
  EXPECT_EQ(TransitionScene::slowSweepMs(600000, 800, 12), TransitionScene::SLOW_SWEEP_MAX_MS);  // 127,200, capped
  EXPECT_EQ(TransitionScene::slowSweepMs(0, 800, 12), 1u);

  // The measured dwell: every millisecond a pixel in the middle of the screen is under a rectangle, counted over one
  // crossing, at least `dwell` periods (less the millisecond the integer clock can lose at either edge).
  for (const uint32_t periodUs : {100000u, 380000u}) {
    for (const uint32_t dwell : {4u, 7u, 12u}) {
      TransitionScene scene(PanelTurn::LandscapeCounterClockwise, 800, 480, 800, 480);
      scene.setMode(SweepMode::Slow, dwell);
      paintAndSettle(scene, periodUs, 0);  // the crossing starts at clock 0
      EXPECT_EQ(scene.rectW(), TransitionScene::RECT_W_SLOW);
      EXPECT_EQ(scene.sweepMs(), TransitionScene::slowSweepMs(periodUs, 800, dwell));
      EXPECT_FALSE(scene.sweepCapped());
      EXPECT_GE(scene.dwellSlotsAt(periodUs), dwell);
      const int top = scene.rectTop(1, 0);
      uint32_t coveredMs = 0;
      for (uint32_t t = 0; t < scene.sweepMs(); ++t) {
        if (scene.targetAt(400, top, t) == TransitionScene::laneShade(1, 0)) ++coveredMs;
      }
      EXPECT_GE(coveredMs + 1, dwell * periodUs / 1000) << periodUs << " " << dwell;
      EXPECT_EQ(scene.rectX(scene.sweepMs()), std::nullopt);  // the pause follows the crossing
      EXPECT_EQ(scene.rectX(scene.sweepMs() + TransitionScene::PAUSE_MS), -TransitionScene::RECT_W_SLOW);
    }
  }
  // Over the cap the dwell falls short, and the scene says so.
  TransitionScene capped(PanelTurn::LandscapeCounterClockwise, 800, 480, 800, 480);
  capped.setMode(SweepMode::Slow, 12);
  paintAndSettle(capped, 600000, 0);
  EXPECT_EQ(capped.sweepMs(), TransitionScene::SLOW_SWEEP_MAX_MS);
  EXPECT_TRUE(capped.sweepCapped());
  EXPECT_EQ(capped.dwellSlotsAt(600000), 11u);  // 48 x 120,000 / 848 = 6,792 ms of 600 ms slots
}

TEST(TransitionScene, SlowWaitsOffScreenUntilThePaintSettlesThenCrossesAtItsLongestSlot) {
  constexpr int W = 800;
  constexpr int HH = 480;
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.setMode(SweepMode::Slow, 7);
  EXPECT_TRUE(scene.slowPending());
  EXPECT_EQ(scene.slowPeriodUs(), 0u);
  // Not measured yet: the rectangles stay just off the left edge whatever the clock says.
  for (const uint32_t ms : {0u, 400u, 5000u, 20000u}) EXPECT_EQ(scene.rectX(ms), -TransitionScene::RECT_W_SLOW) << ms;
  const int top = scene.rectTop(2, 0);
  EXPECT_EQ(scene.targetAt(0, top, 20000), Shade::L);  // no pixel under a rectangle
  // The bands paint from white: every slot that drives pixels offers its period, the crossing waits.
  const struct {
    size_t driven;
    bool driveDone;
    uint32_t periodUs;
  } paint[] = {
      {384000, true, 380000},  // two scans
      {200000, true, 450000},  // the longest delivered slot
      {90000, false, 900000},  // not delivered: no measurement
      {30000, true, 210000},   // one scan, near the end of the paint
  };
  uint32_t clockMs = 0;
  for (const auto& paintSlot : paint) {
    clockMs += 400;
    const SceneStep step = slot(scene, paintSlot.driven, paintSlot.driveDone, paintSlot.periodUs, clockMs);
    EXPECT_FALSE(step.measured) << clockMs;
    EXPECT_FALSE(step.reapply) << clockMs;
    EXPECT_TRUE(scene.slowPending());
    EXPECT_EQ(scene.rectX(clockMs), -TransitionScene::RECT_W_SLOW) << clockMs;  // not entered while painting
  }
  // The first slot with nothing to drive: the paint has settled. The crossing is sized from the paint's longest
  // delivered slot (not the first, nor this short empty one) and starts at this slot's clock; nothing on screen moves,
  // so no reapply.
  clockMs += 100;
  const SceneStep settled = slot(scene, 0, true, 100000, clockMs);
  EXPECT_TRUE(settled.measured);
  EXPECT_FALSE(settled.reapply);
  EXPECT_FALSE(scene.slowPending());
  EXPECT_EQ(scene.slowPeriodUs(), 450000u);
  const uint32_t sweep = scene.sweepMs();
  EXPECT_EQ(sweep, TransitionScene::slowSweepMs(450000, W, 7));
  EXPECT_EQ(scene.rectX(clockMs), -TransitionScene::RECT_W_SLOW);
  EXPECT_EQ(scene.rectX(clockMs + sweep / 2),
            -TransitionScene::RECT_W_SLOW + static_cast<int>(int64_t{W + 48} * (sweep / 2) / sweep));
  // Once per run.
  EXPECT_FALSE(slot(scene, 0, true, 900000, clockMs + 400).measured);
  EXPECT_EQ(scene.sweepMs(), sweep);
  // The model sees no jump: from the held position to the start of the crossing nothing is retargeted.
  {
    SceneBench b(96, HH);
    TransitionScene small(PanelTurn::LandscapeCounterClockwise, 96, HH, 96, HH);
    small.setMode(SweepMode::Slow, 7);
    small.applyAll(b.model, 300, b.preset);
    while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
    const std::vector<uint8_t> painted = b.pixels;
    paintAndSettle(small, 380000, 400);
    small.update(b.model, 300, 400, b.preset);
    EXPECT_EQ(b.pixels, painted);
  }
  // The next run starts held again and measures again; Fast never measures.
  scene.setMode(SweepMode::Slow, 7);
  EXPECT_EQ(scene.rectX(50000), -TransitionScene::RECT_W_SLOW);
  paintAndSettle(scene, 400000, 0);
  EXPECT_EQ(scene.sweepMs(), TransitionScene::slowSweepMs(400000, W, 7));
  scene.setMode(SweepMode::Fast, 7);
  EXPECT_FALSE(slot(scene, 0, true, 400000, 0).measured);
  EXPECT_EQ(scene.sweepMs(), TransitionScene::SWEEP_MS);
}

TEST(TransitionScene, StaticRectanglesStandStillInTheMiddleThird) {
  constexpr int W = 96;
  constexpr int HH = 480;
  SceneBench b(W, HH);
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.setMode(SweepMode::Static, 4);
  EXPECT_EQ(scene.rectW(), 32);
  for (const uint32_t ms : {0u, 2500u, 5500u, 123456u}) EXPECT_EQ(scene.rectX(ms), 32) << ms;
  const int top = scene.rectTop(2, 1);  // band L, lane D
  EXPECT_EQ(scene.targetAt(31, top, 0), Shade::L);
  EXPECT_EQ(scene.targetAt(32, top, 0), Shade::D);
  EXPECT_EQ(scene.targetAt(63, top, 0), Shade::D);
  EXPECT_EQ(scene.targetAt(64, top, 0), Shade::L);
  scene.applyAll(b.model, 0, b.preset);
  while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
  const std::vector<uint8_t> settled = b.pixels;
  scene.update(b.model, 0, 60000, b.preset);  // nothing moves
  EXPECT_EQ(b.pixels, settled);
}

TEST(TransitionScene, StaticSettlesThenFlipsThroughAllTwelveTransitions) {
  constexpr int W = 96;
  constexpr int HH = 480;
  SceneBench b(W, HH);
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.setMode(SweepMode::Static, b.preset.longestRunSlots());
  // The flips' offsets: 0 for the first paint, then +1, +2, +3, +1, ...
  const uint8_t offsets[] = {0, 1, 2, 3, 1, 2, 3};
  for (uint32_t n = 0; n < 7; ++n) EXPECT_EQ(TransitionScene::flipOffset(n), offsets[n]) << n;
  EXPECT_EQ(TransitionScene::shifted(Shade::W, 3), Shade::L);

  // The task's loop without scans: reapply when slotDone says so, one commit per slot.
  bool stale = true;
  bool seen[SHADE_COUNT][SHADE_COUNT] = {};
  std::vector<StaticSettled> settles;
  for (int slot = 0; slot < 400 && settles.size() < 5; ++slot) {
    if (stale) {
      const std::vector<uint8_t> before = b.pixels;
      scene.applyAll(b.model, 0, b.preset);
      stale = false;
      if (!settles.empty()) {
        // Flip n: every pixel moves on by its offset with the full table dose.
        const uint8_t offset = TransitionScene::flipOffset(static_cast<uint32_t>(settles.size()));
        for (int y = 0; y < HH; ++y) {
          for (int x = 0; x < W; ++x) {
            const size_t i = GrayModel::PIXEL_BYTES * (static_cast<size_t>(y) * W + x);
            const auto was = static_cast<uint8_t>(before[i] / GrayModel::LEVEL_STEP);
            const uint8_t to = b.model.target(x, y);
            ASSERT_EQ(to, static_cast<uint8_t>(TransitionScene::shifted(static_cast<Shade>(was), offset)))
                << x << "," << y;
            ASSERT_EQ(b.model.remaining(x, y), b.preset.dose[was][to]);
            if (settles.size() <= 3) seen[was][to] = true;
          }
        }
      }
    }
    const size_t driven = b.model.activeCount();
    b.model.commit(true, true, b.preset);
    const SceneStep step = scene.slotDone(b.model, b.preset, driven, true, 100000, 0);
    EXPECT_EQ(step.reapply, step.settled.has_value());
    EXPECT_FALSE(step.measured);
    if (step.settled) settles.push_back(*step.settled);
    stale = step.reapply;
  }
  ASSERT_EQ(settles.size(), 5u);
  // InkBoy's doses, two a slot while the hold bit is off and more than one is left: the first paint from white is
  // W>K 7 (4 slots); flip 1 (+1) has W>K again, flip 2 (+2) K>L 4 (hold on: 4), flip 3 (+3) K>W 7 (4), flip 4 W>K.
  for (uint32_t n = 0; n < 5; ++n) {
    EXPECT_EQ(settles[n].flip, n);
    EXPECT_EQ(settles[n].offset, offsets[n]);
    EXPECT_EQ(settles[n].slots, 4u) << n;
  }
  // Flips 1 to 3 took the pixels through every one of the twelve transitions.
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      EXPECT_EQ(seen[from][to], from != to) << int(from) << ">" << int(to);
    }
  }
  // The pending flip 5 is +2 on top of 1 + 2 + 3 + 1: a shift of 1.
  EXPECT_EQ(scene.shift(), 1);
  scene.restartFlips();
  EXPECT_EQ(scene.shift(), 0);
  const SceneStep first = slot(scene, 0, true, 100000, 0);
  ASSERT_TRUE(first.settled.has_value());
  EXPECT_EQ(first.settled->flip, 0u);
  EXPECT_EQ(first.settled->slots, 0u);
  EXPECT_EQ(scene.shift(), 1);
  // Not Static: no sequence.
  scene.setMode(SweepMode::Slow, 4);
  EXPECT_EQ(scene.shift(), 0);
  EXPECT_FALSE(slot(scene, 0, true, 100000, 0).settled.has_value());
  EXPECT_EQ(scene.shift(), 0);
}

// ---- holds for photos (dlab-c4) ----------------------------------------------------------------------------------

// The sweep task without scans, as TransitionTestActivity runs it: before each slot the scene's reapply or its move to
// the clock, one commit (both doses for every active pixel), slotDone, the reapply it asks for; the clock moves on by
// tickMs (TICK_MS unless a test sets it) after each slot unless the scene holds it still. `logical` (lw x lh) under
// `turn` on a panel pw x ph.
struct TaskSim {
  static constexpr uint32_t TICK_MS = 100;
  SceneBench b;
  TransitionScene scene;
  uint32_t clockMs = 0;
  uint32_t sceneMs = 0;
  bool stale = true;
  // The clock's step per Running slot (a slot's length).
  uint32_t tickMs = TICK_MS;

  TaskSim(const PanelTurn turn, const int lw, const int lh, const int pw, const int ph, const SweepMode mode,
          const uint32_t dwell, const bool hold)
      : b(pw, ph), scene(turn, lw, lh, pw, ph) {
    scene.setMode(mode, dwell);
    scene.setHold(hold);
  }
  TaskSim(const int w, const int h, const SweepMode mode, const uint32_t dwell, const bool hold)
      : TaskSim(PanelTurn::LandscapeCounterClockwise, w, h, w, h, mode, dwell, hold) {}

  SceneStep slot(const uint32_t periodUs = 100000) {
    if (stale) {
      scene.applyAll(b.model, clockMs, b.preset);
      stale = false;
    } else {
      scene.update(b.model, sceneMs, clockMs, b.preset);
    }
    sceneMs = clockMs;
    const size_t driven = b.model.activeCount();
    // A Ladder stage's slots run with every hold bit set, as the task runs them (one dose a slot); every other slot on
    // the preset as it is.
    b.model.commit(true, true, scene.slotPreset(b.preset));
    const SceneStep step = scene.slotDone(b.model, b.preset, driven, true, periodUs, clockMs);
    if (step.reapply) stale = true;
    if (!scene.clockStill()) clockMs += tickMs;
    return step;
  }
  // The model pixel's target (panel coordinates; the logical ones under LandscapeCounterClockwise).
  uint8_t target(const int x, const int y) const { return b.model.target(x, y); }
};

bool inMarker(const TransitionScene& scene, const int lx, const int ly) {
  const ScreenRect m = scene.markerRect();
  return lx >= m.x && lx < m.x + m.w && ly >= m.y && ly < m.y + m.h;
}

TEST(TransitionScene, HoldMarkerSitsInTheBottomRightCornerInEveryTurn) {
  constexpr int PW = 64;
  constexpr int PH = 160;
  std::vector<std::pair<uint32_t, uint32_t>> markerSlotsByTurn;
  for (const PanelTurn turn : TURNS) {
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    TaskSim sim(turn, lw, lh, PW, PH, SweepMode::Static, 4, true);
    const ScreenRect m = sim.scene.markerRect();
    EXPECT_EQ(m.x, lw - TransitionScene::MARKER_PX) << int(turn);
    EXPECT_EQ(m.y, lh - TransitionScene::MARKER_PX) << int(turn);
    EXPECT_EQ(m.w, TransitionScene::MARKER_PX);
    EXPECT_EQ(m.h, TransitionScene::MARKER_PX);
    EXPECT_GE(m.y, 3 * sim.scene.bandHeight()) << int(turn);  // inside the bottom band
    EXPECT_EQ(sim.scene.markerShade(), Shade::W);             // not out: its band's shade
    // Paint, settle: the marker goes out to the opposite of the bottom band's W, D, and nowhere else.
    std::optional<HoldStart> hold;
    for (int n = 0; n < 50 && !hold; ++n) hold = sim.slot().hold;
    ASSERT_TRUE(hold.has_value()) << int(turn);
    EXPECT_EQ(sim.scene.markerShade(), Shade::D);
    // The corner of the bottom band (W) around it (this screen has no lanes, so the marker is the square).
    for (int ly = std::max(lh - 24, 3 * sim.scene.bandHeight()); ly < lh; ++ly) {
      for (int lx = lw - 24; lx < lw; ++lx) {
        int px = 0;
        int py = 0;
        rendererRotate(turn, lx, ly, &px, &py, PW, PH);
        const Shade wanted = inMarker(sim.scene, lx, ly) ? Shade::D : Shade::W;
        EXPECT_EQ(sim.scene.targetAt(lx, ly, sim.clockMs), wanted) << int(turn) << " " << lx << "," << ly;
        EXPECT_EQ(sim.b.model.target(px, py), static_cast<uint8_t>(wanted)) << int(turn) << " " << lx << "," << ly;
      }
    }
    // Out to held and back to resumed: markerSettled reads the marker's panel pixels in this turn, so it says settled
    // only when every rotated pixel under markerRect() has had its last dose.
    const auto expectMarker = [&sim, turn, lw, lh](const Shade shade) {
      for (int ly = 0; ly < lh; ++ly) {
        for (int lx = 0; lx < lw; ++lx) {
          if (!inMarker(sim.scene, lx, ly)) continue;
          int px = 0;
          int py = 0;
          rendererRotate(turn, lx, ly, &px, &py, PW, PH);
          EXPECT_EQ(sim.b.model.remaining(px, py), 0) << int(turn) << " " << lx << "," << ly;
          EXPECT_EQ(sim.b.model.target(px, py), static_cast<uint8_t>(shade)) << int(turn) << " " << lx << "," << ly;
        }
      }
    };
    SceneStep step;
    uint32_t outSlots = 0;
    while (!step.held && outSlots < 50) {
      EXPECT_FALSE(sim.scene.markerSettled(sim.b.model)) << int(turn) << " out " << outSlots;
      step = sim.slot();
      ++outSlots;
    }
    ASSERT_TRUE(step.held) << int(turn);
    EXPECT_EQ(step.markerSlots, outSlots);
    expectMarker(Shade::D);
    ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    uint32_t backSlots = 0;
    step = SceneStep();
    while (!step.resumed && backSlots < 50) {
      EXPECT_FALSE(sim.scene.markerSettled(sim.b.model)) << int(turn) << " back " << backSlots;
      step = sim.slot();
      ++backSlots;
    }
    ASSERT_TRUE(step.resumed) << int(turn);
    expectMarker(Shade::W);
    markerSlotsByTurn.push_back({outSlots, backSlots});
  }
  // The same dose in every turn: the same slots out and back.
  ASSERT_EQ(markerSlotsByTurn.size(), 4u);
  for (const auto& slots : markerSlotsByTurn) {
    EXPECT_EQ(slots, markerSlotsByTurn[0]);
    EXPECT_GE(slots.first, 2u);  // W>D, 3 doses, follow-up on: two slots
  }
}

TEST(TransitionScene, StaticHoldsAtEverySettleAndFlipsOnlyOnceTheMarkerIsBack) {
  constexpr int W = 96;
  constexpr int HH = 480;
  const uint32_t dwell = GrayPreset::inkBoyDefaults().longestRunSlots();
  TaskSim sim(W, HH, SweepMode::Static, dwell, true);
  EXPECT_FALSE(sim.scene.resumeHold(sim.b.model, sim.b.preset));  // Running: nothing to resume
  // The same scene with Hold off: the settles' slot counts the marker's slots must not change.
  std::vector<uint32_t> twinSlots;
  {
    TaskSim twin(W, HH, SweepMode::Static, dwell, false);
    for (int n = 0; n < 200 && twinSlots.size() < 4; ++n) {
      const SceneStep step = twin.slot();
      if (step.settled) twinSlots.push_back(step.settled->slots);
    }
    ASSERT_EQ(twinSlots.size(), 4u);
  }
  const auto targets = [&sim] {
    std::vector<uint8_t> t(static_cast<size_t>(W) * HH);
    for (int y = 0; y < HH; ++y)
      for (int x = 0; x < W; ++x) t[static_cast<size_t>(y) * W + x] = sim.target(x, y);
    return t;
  };
  for (uint32_t flip = 0; flip < 4; ++flip) {
    // Paint (or flip) until the pattern settles: the settle line and the spot in the same slot, no flip yet.
    SceneStep step;
    int slots = 0;
    while (!step.hold && slots++ < 50) step = sim.slot();
    ASSERT_TRUE(step.hold.has_value()) << flip;
    EXPECT_EQ(step.hold->spot, HoldSpot::StaticSettle);
    EXPECT_EQ(step.hold->count, flip);
    ASSERT_TRUE(step.settled.has_value());
    EXPECT_EQ(step.settled->flip, flip);
    EXPECT_EQ(step.settled->slots, twinSlots[flip]) << flip;  // the marker's slots never counted
    EXPECT_FALSE(step.reapply);
    EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerOut);
    EXPECT_TRUE(sim.scene.clockStill());
    const uint8_t shift = sim.scene.shift();
    const Shade band = TransitionScene::shifted(Shade::W, shift);
    EXPECT_EQ(sim.scene.markerShade(), TransitionScene::shifted(band, 2));
    // The marker's slots: held only in the slot it settles, nothing settles or flips meanwhile.
    std::vector<uint8_t> pattern = targets();
    uint32_t markerSlots = 0;
    do {
      EXPECT_FALSE(sim.scene.markerSettled(sim.b.model));
      step = sim.slot(900000);
      ++markerSlots;
      EXPECT_FALSE(step.settled.has_value());
      EXPECT_FALSE(step.hold.has_value());
      EXPECT_FALSE(step.reapply);
    } while (!step.held && markerSlots < 50);
    ASSERT_TRUE(step.held);
    EXPECT_EQ(step.markerSlots, markerSlots);
    EXPECT_TRUE(sim.scene.markerSettled(sim.b.model));
    EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::Held);
    EXPECT_EQ(sim.scene.shift(), shift);
    EXPECT_EQ(targets(), pattern);
    // Resume: the marker back to its band's shade, every other pixel's target as it was.
    EXPECT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    EXPECT_FALSE(sim.scene.resumeHold(sim.b.model, sim.b.preset));  // once
    EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerBack);
    EXPECT_EQ(sim.scene.markerShade(), band);
    for (int y = 0; y < HH; ++y) {
      for (int x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        if (inMarker(sim.scene, x, y)) {
          EXPECT_EQ(sim.target(x, y), static_cast<uint8_t>(band)) << x << "," << y;
          pattern[i] = static_cast<uint8_t>(band);
        } else {
          ASSERT_EQ(sim.target(x, y), pattern[i]) << x << "," << y;
        }
      }
    }
    // The marker's slots back: the pattern stays until the slot it settles, which flips.
    markerSlots = 0;
    do {
      EXPECT_TRUE(sim.scene.clockStill());
      EXPECT_FALSE(sim.scene.markerSettled(sim.b.model));
      EXPECT_EQ(targets(), pattern);
      step = sim.slot(900000);
      ++markerSlots;
      EXPECT_FALSE(step.settled.has_value());
    } while (!step.resumed && markerSlots < 50);
    ASSERT_TRUE(step.resumed);
    EXPECT_TRUE(sim.scene.markerSettled(sim.b.model));
    EXPECT_EQ(step.markerSlots, markerSlots);
    EXPECT_TRUE(step.reapply);
    EXPECT_FALSE(sim.scene.clockStill());
    EXPECT_EQ(sim.scene.shift(), (shift + TransitionScene::flipOffset(flip + 1)) % SHADE_COUNT);
  }
}

TEST(TransitionScene, HoldOffNeverHoldsAndStaticFlipsAtOnce) {
  for (const SweepMode mode : {SweepMode::Static, SweepMode::Slow, SweepMode::Fast}) {
    TaskSim sim(96, 480, mode, 4, false);
    uint32_t settles = 0;
    // Slow and Fast: slots that passed a spot (Slow's measured paint, slots with the rectangles wholly on-screen, the
    // pauses), so the absence of holds is not for want of spots.
    uint32_t measured = 0;
    uint32_t onScreenSlots = 0;
    uint32_t pauseSlots = 0;
    for (int n = 0; n < 200; ++n) {  // 20 s of clock: every mode passes its spots several times
      const bool crossing = mode != SweepMode::Static && !sim.scene.slowPending();
      const std::optional<int> x = sim.scene.rectX(sim.clockMs);
      if (crossing && !x) ++pauseSlots;
      if (crossing && x && *x >= 0 && *x + sim.scene.rectW() <= 96) ++onScreenSlots;
      const SceneStep step = sim.slot();
      if (step.measured) ++measured;
      EXPECT_FALSE(step.hold.has_value()) << int(mode) << " " << n;
      EXPECT_FALSE(step.held);
      EXPECT_FALSE(sim.scene.clockStill());
      if (step.settled) {
        ++settles;
        EXPECT_TRUE(step.reapply);  // dlab-c3: the flip follows the settle at once
      }
      EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::Running);
      // The marker never out: its shade is the bottom band's as shown.
      EXPECT_EQ(sim.scene.markerShade(), TransitionScene::shifted(Shade::W, sim.scene.shift()));
    }
    if (mode == SweepMode::Static) EXPECT_GE(settles, 4u);
    if (mode == SweepMode::Slow) {
      EXPECT_EQ(measured, 1u);
      EXPECT_GE(onScreenSlots, 8u);  // two crossings' worth of the dwell's 4
    }
    if (mode != SweepMode::Static) EXPECT_GE(pauseSlots, 2u) << int(mode);
  }
}

// Runs a Slow or Fast TaskSim with Hold on until `count` holds, resuming each as soon as it is held; records each
// hold's spot, its clock, and, for Slow's dwell, the Running slots of its crossing with the rectangles' left edge
// on-screen; and, into `skipped` when given, each spot a crossing passed without holding.
struct SpotSeen {
  HoldStart hold;
  uint32_t clockMs;
  std::optional<int> x;
  uint32_t enteredSlots;
};

std::vector<SpotSeen> runToHolds(TaskSim& sim, const size_t count, std::vector<SkippedSpot>* skipped = nullptr) {
  std::vector<SpotSeen> seen;
  uint32_t entered = 0;
  std::optional<uint32_t> heldAtMs;
  for (int n = 0; n < 2000 && seen.size() < count; ++n) {
    const bool running = !sim.scene.clockStill();
    const uint32_t ms = sim.clockMs;
    const std::optional<int> x = sim.scene.rectX(ms);
    if (running && !sim.scene.slowPending()) {
      if (!x) entered = 0;  // the pause: the next crossing counts afresh
      if (x && *x >= 0) ++entered;
    }
    // The marker's slots report a long period: none of it may reach Slow's measurement.
    const SceneStep step = sim.slot(running ? 100000 : 900000);
    if (step.skipped && skipped) skipped->push_back(*step.skipped);
    if (step.hold) {
      seen.push_back(SpotSeen{*step.hold, ms, x, entered});
      heldAtMs = ms;
    }
    if (step.held) {
      EXPECT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    }
    if (step.resumed) {
      // The clock stood still from the spot through the marker's slots out and back.
      EXPECT_EQ(ms, heldAtMs.value_or(UINT32_MAX));
      EXPECT_FALSE(step.reapply);
    }
  }
  return seen;
}

TEST(TransitionScene, SlowHoldsAtThePaintAtTheFullDwellAndAtTheCrossingsEnd) {
  constexpr int W = 96;
  constexpr uint32_t DWELL = 4;
  TaskSim sim(W, 480, SweepMode::Slow, DWELL, true);
  const std::vector<SpotSeen> seen = runToHolds(sim, 5);
  ASSERT_EQ(seen.size(), 5u);
  // The painted bands, in the slot that measured: the rectangles still off the left edge.
  EXPECT_EQ(seen[0].hold.spot, HoldSpot::SlowPainted);
  EXPECT_EQ(seen[0].hold.count, 0u);
  EXPECT_EQ(seen[0].x, -TransitionScene::RECT_W_SLOW);
  // The marker's slots (900 ms each) never reached the measurement: the crossing is the paint's 100 ms slots'.
  EXPECT_EQ(sim.scene.slowPeriodUs(), 100000u);
  EXPECT_EQ(sim.scene.sweepMs(), TransitionScene::slowSweepMs(100000, W, DWELL));
  // Then per crossing: the DWELL-th slot with the left edge on-screen, the right edge still on; the pause's first.
  for (uint32_t crossing = 1; crossing <= 2; ++crossing) {
    const SpotSeen& dwelt = seen[2 * crossing - 1];
    EXPECT_EQ(dwelt.hold.spot, HoldSpot::SlowDwelt) << crossing;
    EXPECT_EQ(dwelt.hold.count, crossing);
    ASSERT_TRUE(dwelt.x.has_value());
    EXPECT_GE(*dwelt.x, 0);
    EXPECT_LE(*dwelt.x + TransitionScene::RECT_W_SLOW, W);
    EXPECT_EQ(dwelt.enteredSlots, DWELL) << crossing;
    const SpotSeen& crossed = seen[2 * crossing];
    EXPECT_EQ(crossed.hold.spot, HoldSpot::SlowCrossed) << crossing;
    EXPECT_EQ(crossed.hold.count, crossing);
    EXPECT_FALSE(crossed.x.has_value());
    // The first slot of the pause: one tick earlier the rectangles were still crossing.
    EXPECT_TRUE(sim.scene.rectX(crossed.clockMs - TaskSim::TICK_MS).has_value()) << crossing;
  }
}

TEST(TransitionScene, FastHoldsOncePerSweepInItsPause) {
  TaskSim sim(96, 480, SweepMode::Fast, 4, true);
  const std::vector<SpotSeen> seen = runToHolds(sim, 3);
  ASSERT_EQ(seen.size(), 3u);
  const uint32_t cycle = TransitionScene::SWEEP_MS + TransitionScene::PAUSE_MS;
  for (uint32_t n = 0; n < 3; ++n) {
    EXPECT_EQ(seen[n].hold.spot, HoldSpot::FastPause);
    EXPECT_EQ(seen[n].hold.count, n + 1);
    EXPECT_EQ(seen[n].clockMs, n * cycle + TransitionScene::SWEEP_MS);  // the pause's first 100 ms slot
    EXPECT_FALSE(seen[n].x.has_value());
  }
}

TEST(TransitionScene, SessionCapCleanDropsAHoldAndStaticHoldsAgainAtFlipZero) {
  constexpr int W = 96;
  constexpr int HH = 480;
  TaskSim sim(W, HH, SweepMode::Static, 4, true);
  EXPECT_FALSE(sim.scene.cancelHold().has_value());  // Running: nothing to drop
  SceneStep step;
  for (int n = 0; n < 50 && !step.held; ++n) step = sim.slot();  // flip 0 settled and held
  ASSERT_TRUE(step.held);
  ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  sim.slot();  // one slot of the marker's way back
  ASSERT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerBack);
  // The cap's clean, as the task runs it: white, the hold dropped, Static's sequence restarted, the scene reapplied.
  sim.b.model.reset(Shade::W);
  const std::optional<DroppedHold> back = sim.scene.cancelHold();
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->spot, HoldSpot::StaticSettle);
  EXPECT_EQ(back->drop, HoldDrop::WayBackCutOff);  // the photo was taken
  EXPECT_EQ(back->markerSlots, 1u);
  EXPECT_FALSE(sim.scene.cancelHold().has_value());
  sim.scene.restartFlips();
  sim.stale = true;
  EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::Running);
  EXPECT_FALSE(sim.scene.clockStill());
  EXPECT_EQ(sim.scene.markerShade(), Shade::W);
  // No pending flip survives: the repaint settles as flip 0 and holds again; the pattern is flip 0's.
  step = SceneStep();
  for (int n = 0; n < 50 && !step.hold; ++n) step = sim.slot();
  ASSERT_TRUE(step.hold.has_value());
  ASSERT_TRUE(step.settled.has_value());
  EXPECT_EQ(step.settled->flip, 0u);
  EXPECT_EQ(step.hold->count, 0u);
  EXPECT_EQ(sim.scene.shift(), 0);
  EXPECT_EQ(sim.target(0, 0), static_cast<uint8_t>(Shade::K));
  // Dropped in MarkerOut too: Static holds again at flip 0; the marker's target is the band's again once the scene is
  // reapplied.
  const std::optional<DroppedHold> out = sim.scene.cancelHold();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->spot, HoldSpot::StaticSettle);
  EXPECT_EQ(out->drop, HoldDrop::StaticAgainAtFlipZero);
  EXPECT_EQ(out->markerSlots, 0u);
  sim.scene.applyAll(sim.b.model, sim.clockMs, sim.b.preset);
  EXPECT_EQ(sim.target(W - 1, HH - 1), static_cast<uint8_t>(Shade::W));
}

TEST(TransitionScene, MarkerIsADashBelowTheLastLaneAtFullSizeInEveryTurn) {
  constexpr int PW = 800;
  constexpr int PH = 480;
  for (const PanelTurn turn : TURNS) {
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    SceneBench b(PW, PH);
    TransitionScene scene(turn, lw, lh, PW, PH);
    scene.setMode(SweepMode::Static, 4);
    scene.setHold(true);
    const ScreenRect m = scene.markerRect();
    const int laneBottom = scene.rectTop(TransitionScene::BANDS - 1, TransitionScene::LANES - 1) + scene.rectHeight();
    // 480 high: the last rectangle ends at row 472, an 8-row margin; 800 high: at row 790, 10 rows.
    EXPECT_EQ(m.h, lh == 480 ? 8 : 10) << int(turn);
    EXPECT_EQ(m.w, TransitionScene::MARKER_W);
    EXPECT_EQ(m.x, lw - TransitionScene::MARKER_W);
    EXPECT_EQ(m.y + m.h, lh);
    EXPECT_GE(m.y, laneBottom) << int(turn);                            // strictly below the last lane's rectangle rows
    EXPECT_GE(m.y, (TransitionScene::BANDS - 1) * scene.bandHeight());  // inside the bottom band
    // The pattern painted and settled, then a settle with Hold on: the marker's pixels go out to D over the W band,
    // nothing around it moves, and it maps to the panel as every other pixel does.
    scene.applyAll(b.model, 0, b.preset);
    while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
    ASSERT_TRUE(scene.slotDone(b.model, b.preset, 0, true, 100000, 0).hold.has_value());
    uint32_t out = 0;
    for (int ly = laneBottom - 8; ly < lh; ++ly) {
      for (int lx = lw - 48; lx < lw; ++lx) {
        int px = 0;
        int py = 0;
        rendererRotate(turn, lx, ly, &px, &py, PW, PH);
        const bool in = inMarker(scene, lx, ly);
        const Shade wanted = in ? Shade::D : scene.targetAt(lx, ly, 0);
        EXPECT_EQ(scene.targetAt(lx, ly, 0), wanted);
        ASSERT_EQ(b.model.target(px, py), static_cast<uint8_t>(wanted)) << int(turn) << " " << lx << "," << ly;
        if (in) {
          ++out;
        } else {
          ASSERT_EQ(b.model.remaining(px, py), 0) << int(turn) << " " << lx << "," << ly;
        }
      }
    }
    EXPECT_EQ(out, static_cast<uint32_t>(m.w * m.h)) << int(turn);
  }
}

TEST(TransitionScene, PressDuringTheMarkersSlotsDoesNotResume) {
  // Next during the marker's slots pauses the task as usual (the screen's toggle); the scene's resume acts only on a
  // hold, so the phase carries on when the run does.
  TaskSim sim(96, 480, SweepMode::Static, 4, true);
  SceneStep step;
  for (int n = 0; n < 50 && !step.hold; ++n) step = sim.slot();
  ASSERT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerOut);
  EXPECT_FALSE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerOut);
  EXPECT_EQ(sim.scene.markerShade(), Shade::D);
  for (int n = 0; n < 50 && !step.held; ++n) step = sim.slot();
  ASSERT_TRUE(step.held);
  ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  EXPECT_FALSE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerBack);
  EXPECT_EQ(sim.scene.markerShade(), Shade::W);
}

// The task's start of a new run on the same scene: the mode and the hold set again, the clock and the model back to 0
// and white.
void restartRun(TaskSim& sim, const SweepMode mode) {
  sim.scene.setMode(mode, 4);
  sim.scene.setHold(true);
  sim.clockMs = 0;
  sim.sceneMs = 0;
  sim.stale = true;
  sim.b.model.reset(Shade::W);
}

TEST(TransitionScene, SetModeRestartsTheCrossingSpots) {
  {
    TaskSim sim(96, 480, SweepMode::Fast, 4, true);
    ASSERT_EQ(runToHolds(sim, 1).size(), 1u);
    restartRun(sim, SweepMode::Fast);
    const std::vector<SpotSeen> seen = runToHolds(sim, 1);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].hold.spot, HoldSpot::FastPause);
    EXPECT_EQ(seen[0].hold.count, 1u);
    EXPECT_EQ(seen[0].clockMs, TransitionScene::SWEEP_MS);
  }
  {
    TaskSim sim(96, 480, SweepMode::Slow, 4, true);
    ASSERT_EQ(runToHolds(sim, 2).back().hold.spot, HoldSpot::SlowDwelt);
    restartRun(sim, SweepMode::Slow);
    const std::vector<SpotSeen> seen = runToHolds(sim, 2);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].hold.spot, HoldSpot::SlowPainted);
    EXPECT_EQ(seen[1].hold.spot, HoldSpot::SlowDwelt);
    EXPECT_EQ(seen[1].hold.count, 1u);
  }
}

TEST(TransitionScene, SlowSkipsTheDwellSpotWhenTheRectanglesReachTheRightEdgeFirst) {
  // 60 px wide: the dwell's 4th slot with the left edge on-screen has x = 36, so the right edge (84) is already off.
  TaskSim sim(60, 480, SweepMode::Slow, 4, true);
  std::vector<SkippedSpot> skipped;
  const std::vector<SpotSeen> seen = runToHolds(sim, 3, &skipped);
  ASSERT_EQ(seen.size(), 3u);
  // Each crossing logs its dwell spot as skipped at the right edge.
  ASSERT_EQ(skipped.size(), 2u);
  for (uint32_t n = 0; n < 2; ++n) {
    EXPECT_EQ(skipped[n].spot, HoldSpot::SlowDwelt);
    EXPECT_EQ(skipped[n].count, n + 1);
    EXPECT_EQ(skipped[n].reason, SpotSkip::RightEdge);
  }
  EXPECT_EQ(seen[0].hold.spot, HoldSpot::SlowPainted);
  EXPECT_EQ(seen[1].hold.spot, HoldSpot::SlowCrossed);
  EXPECT_EQ(seen[1].hold.count, 1u);
  EXPECT_EQ(seen[2].hold.spot, HoldSpot::SlowCrossed);
  EXPECT_EQ(seen[2].hold.count, 2u);
}

TEST(TransitionScene, SessionCapCleanInSlowOrFastSkipsTheDroppedSpot) {
  {
    TaskSim sim(96, 480, SweepMode::Fast, 4, true);
    ASSERT_EQ(runToHolds(sim, 1).size(), 1u);  // the first pause, the marker going out
    sim.b.model.reset(Shade::W);
    const std::optional<DroppedHold> dropped = sim.scene.cancelHold();
    ASSERT_TRUE(dropped.has_value());
    EXPECT_EQ(dropped->spot, HoldSpot::FastPause);
    EXPECT_EQ(dropped->drop, HoldDrop::SkippedThisCrossing);
    sim.stale = true;
    const std::vector<SpotSeen> seen = runToHolds(sim, 1);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].hold.spot, HoldSpot::FastPause);
    EXPECT_EQ(seen[0].hold.count, 2u);  // this pause is not held again
    EXPECT_EQ(seen[0].clockMs, 2 * TransitionScene::SWEEP_MS + TransitionScene::PAUSE_MS);
  }
  {
    TaskSim sim(96, 480, SweepMode::Slow, 4, true);
    ASSERT_EQ(runToHolds(sim, 2).back().hold.spot, HoldSpot::SlowDwelt);
    const uint32_t sweep = sim.scene.sweepMs();
    sim.b.model.reset(Shade::W);
    const std::optional<DroppedHold> dropped = sim.scene.cancelHold();
    ASSERT_TRUE(dropped.has_value());
    EXPECT_EQ(dropped->spot, HoldSpot::SlowDwelt);
    EXPECT_EQ(dropped->drop, HoldDrop::SkippedThisCrossing);
    sim.stale = true;
    // The bands repaint under the crossing, not measured again: the next hold is the crossing's end.
    const std::vector<SpotSeen> seen = runToHolds(sim, 1);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].hold.spot, HoldSpot::SlowCrossed);
    EXPECT_EQ(seen[0].hold.count, 1u);
    EXPECT_FALSE(sim.scene.slowPending());
    EXPECT_EQ(sim.scene.sweepMs(), sweep);
  }
}

TEST(TransitionScene, APauseNoSlotFellInIsLoggedAsSkipped) {
  // Fast with 1.5 s slots: 0, 1.5, 3, 4.5, then 6 s, the next crossing; no slot starts in the 5 to 6 s pause.
  TaskSim sim(96, 480, SweepMode::Fast, 4, true);
  sim.tickMs = 1500;
  std::vector<SkippedSpot> skipped;
  EXPECT_TRUE(runToHolds(sim, 1, &skipped).empty());  // 2,000 slots, never a hold
  ASSERT_GE(skipped.size(), 3u);
  for (uint32_t n = 0; n < 3; ++n) {
    EXPECT_EQ(skipped[n].spot, HoldSpot::FastPause);
    EXPECT_EQ(skipped[n].count, n + 1);
    EXPECT_EQ(skipped[n].reason, SpotSkip::PauseOutrun);
  }
  // Hold off: nothing is a skipped hold.
  TaskSim off(96, 480, SweepMode::Fast, 4, false);
  off.tickMs = 1500;
  for (int n = 0; n < 20; ++n) EXPECT_FALSE(off.slot().skipped.has_value());
}

TEST(TransitionScene, ShadeLettersAndLogNames) {
  EXPECT_EQ(shadeLetter(Shade::K), 'K');
  EXPECT_EQ(shadeLetter(Shade::D), 'D');
  EXPECT_EQ(shadeLetter(Shade::L), 'L');
  EXPECT_EQ(shadeLetter(Shade::W), 'W');
  EXPECT_STREQ(spotSkipName(SpotSkip::RightEdge), "the rectangles reached the right edge first");
  EXPECT_STREQ(spotSkipName(SpotSkip::PauseOutrun), "no slot started in the pause");
  EXPECT_STREQ(holdDropName(HoldDrop::StaticAgainAtFlipZero), "holds again at flip 0");
  EXPECT_STREQ(holdDropName(HoldDrop::SkippedThisCrossing), "skipped for this crossing");
  EXPECT_STREQ(holdDropName(HoldDrop::WayBackCutOff), "photo taken, the marker's way back cut off");
  EXPECT_STREQ(holdDropName(HoldDrop::LadderFromBase), "the ladder restarts from the base");
}

TEST(TransitionScene, HoldSpotNamesForTheLog) {
  EXPECT_STREQ(holdSpotName(HoldSpot::StaticSettle), "static settle");
  EXPECT_STREQ(holdSpotName(HoldSpot::SlowPainted), "slow painted");
  EXPECT_STREQ(holdSpotName(HoldSpot::SlowDwelt), "slow dwelt");
  EXPECT_STREQ(holdSpotName(HoldSpot::SlowCrossed), "slow crossed");
  EXPECT_STREQ(holdSpotName(HoldSpot::FastPause), "fast pause");
  EXPECT_STREQ(holdSpotName(HoldSpot::Ladder), "ladder");
}

// ---- dlab-g1: the Ladder ------------------------------------------------------------------------------------------

TEST(TransitionScene, LadderLayoutAtTheX4ProsWidthsAndOnNarrowScreens) {
  const auto expectLayout = [](const int w, const int columns, const int width, const int gutter) {
    const LadderLayout l = TransitionScene::ladderLayout(w);
    EXPECT_EQ(l.columns, columns) << w;
    EXPECT_EQ(l.width, width) << w;
    EXPECT_EQ(l.gutter, gutter) << w;
    EXPECT_LE(l.columns * (l.width + l.gutter), w > 0 ? w : 0) << w;  // the columns fit
  };
  // A 1 px gutter of the origin after every column wherever the pitch has room for one.
  expectLayout(480, 24, 19, 1);  // portrait: pitch 20
  expectLayout(800, 24, 32, 1);  // landscape: pitch 33, 8 px left over
  expectLayout(72, 24, 2, 1);
  expectLayout(48, 24, 1, 1);
  expectLayout(47, 24, 1, 0);  // under 48: 1 px columns, no gutter
  expectLayout(30, 24, 1, 0);
  expectLayout(20, 20, 1, 0);  // under 24: as many as fit
  expectLayout(1, 1, 1, 0);
  expectLayout(0, 0, 0, 0);
  expectLayout(-5, 0, 0, 0);
}

TEST(TransitionScene, LadderModeHasNoRectanglesAndSlotPresetIsHeldOnlyWhilePulsing) {
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, 800, 480, 800, 480);
  scene.setMode(SweepMode::Ladder, 4);
  EXPECT_EQ(scene.mode(), SweepMode::Ladder);
  EXPECT_FALSE(scene.rectX(0).has_value());
  EXPECT_FALSE(scene.rectX(3000).has_value());
  EXPECT_EQ(scene.sweepMs(), 0u);
  EXPECT_EQ(scene.rectW(), 0);  // no rectangles: the layout carries the columns
  EXPECT_FALSE(scene.slowPending());
  EXPECT_EQ(scene.ladderStage(), LadderStage::Base);
  EXPECT_FALSE(scene.ladderPulsing());
  EXPECT_EQ(scene.markerBandShade(), Shade::K);
  // The Ladder holds whatever the Hold row says; the other modes only with it on.
  EXPECT_FALSE(scene.holdOn());
  EXPECT_TRUE(scene.holdsAtSpots());
  scene.setMode(SweepMode::Static, 4);
  EXPECT_FALSE(scene.holdsAtSpots());
  scene.setMode(SweepMode::Ladder, 4);
  // update() moves nothing: no rectangles.
  SceneBench b(800, 480);
  scene.update(b.model, 0, 5000, b.preset);
  EXPECT_EQ(b.model.activeCount(), 0u);
  // heldRhythm: every hold bit, everything else the preset's.
  const GrayPreset p = GrayPreset::ultraChipDefaults();
  const GrayPreset held = TransitionScene::heldRhythm(p);
  EXPECT_EQ(held.holdMask, 0xFFFF);
  EXPECT_TRUE(held.valid());
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    EXPECT_EQ(held.doseAt(i), p.doseAt(i));
    EXPECT_TRUE(held.holdAt(i));
  }
  EXPECT_EQ(held.rateIndex, p.rateIndex);
  EXPECT_EQ(held.swapPolarity, p.swapPolarity);
  EXPECT_EQ(held.retargetDarkPercent, p.retargetDarkPercent);
  EXPECT_EQ(held.retargetLightPercent, p.retargetLightPercent);
  EXPECT_EQ(held.historyMode, p.historyMode);
  EXPECT_EQ(held.weakWhiteToLight, p.weakWhiteToLight);
  // slotPreset: the preset itself at the base, the held copy once stage 1 pulses.
  EXPECT_EQ(&scene.slotPreset(p), &p);
  TaskSim sim(96, 72, SweepMode::Ladder, 4, true);
  EXPECT_EQ(&sim.scene.slotPreset(p), &p);
  for (int n = 0; n < 50 && !sim.scene.ladderPulsing(); ++n) sim.slot();
  ASSERT_TRUE(sim.scene.ladderPulsing());
  const GrayPreset& pulsing = sim.scene.slotPreset(p);
  EXPECT_NE(&pulsing, &p);
  EXPECT_EQ(pulsing.holdMask, 0xFFFF);
  EXPECT_EQ(pulsing.rateIndex, p.rateIndex);
}

// The X4 Pro's screen in each turn: the columns' rectangles (D14's figures with the gutter), the marker a MARKER_W x
// MARKER_PX dash at the bottom-right under them, and the base: the top half white, everything below it black, column
// 24 driven first (end-aligned: its 24 pulses are the whole stage).
TEST(TransitionScene, LadderColumnsAndMarkerInEveryTurnAtFullSize) {
  constexpr int PW = 800;
  constexpr int PH = 480;
  for (const PanelTurn turn : TURNS) {
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    TaskSim sim(turn, lw, lh, PW, PH, SweepMode::Ladder, 4, true);
    const ScreenRect m = sim.scene.markerRect();
    EXPECT_EQ(m.x, lw - TransitionScene::MARKER_W) << int(turn);
    EXPECT_EQ(m.y, lh - TransitionScene::MARKER_PX) << int(turn);
    EXPECT_EQ(m.w, TransitionScene::MARKER_W) << int(turn);
    EXPECT_EQ(m.h, TransitionScene::MARKER_PX) << int(turn);
    const int rows = lh - TransitionScene::MARKER_PX;
    ASSERT_EQ(sim.scene.ladderHeight(), rows) << int(turn);
    const int pitch = portrait(turn) ? 20 : 33;
    const int width = pitch - 1;
    for (int n = 1; n <= TransitionScene::LADDER_STEPS; ++n) {
      for (const LadderHalf half : {LadderHalf::FromWhite, LadderHalf::FromBlack}) {
        const ScreenRect c = sim.scene.ladderColumn(half, n);
        EXPECT_EQ(c.x, (n - 1) * pitch) << int(turn) << " " << n;
        EXPECT_EQ(c.w, width);
        EXPECT_EQ(c.y, half == LadderHalf::FromWhite ? 0 : rows / 2);
        EXPECT_EQ(c.h, half == LadderHalf::FromWhite ? rows / 2 : rows - rows / 2);
        EXPECT_LE(c.y + c.h, m.y) << "column " << n << " reaches the marker's rows";
      }
    }
    EXPECT_EQ(sim.scene.ladderColumn(LadderHalf::FromWhite, 0).w, 0);
    EXPECT_EQ(sim.scene.ladderColumn(LadderHalf::FromWhite, 25).w, 0);
    SceneStep step;
    int slots = 0;
    while (!step.ladderSettled && slots++ < 50) step = sim.slot();
    ASSERT_TRUE(step.ladderSettled.has_value()) << int(turn);
    EXPECT_EQ(step.ladderSettled->stage, LadderStage::Base);
    EXPECT_GT(step.ladderSettled->slots, 0u);
    EXPECT_FALSE(step.hold.has_value());
    EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Ladder);
    EXPECT_TRUE(sim.scene.ladderPulsing());
    for (int ly = 0; ly < lh; ++ly) {
      const bool top = ly < rows / 2;
      for (int lx = 0; lx < lw; ++lx) {
        const bool column24 = ly < rows && lx >= 23 * pitch && lx < 23 * pitch + width;
        int px = 0;
        int py = 0;
        rendererRotate(turn, lx, ly, &px, &py, PW, PH);
        const uint8_t origin = static_cast<uint8_t>(top ? Shade::W : Shade::K);
        const uint8_t end = static_cast<uint8_t>(top ? Shade::K : Shade::W);
        ASSERT_EQ(sim.b.model.level(px, py), top ? 192 : 0) << int(turn) << " " << lx << "," << ly;
        ASSERT_EQ(sim.b.model.remaining(px, py), column24 ? 24 : 0) << int(turn) << " " << lx << "," << ly;
        ASSERT_EQ(sim.b.model.target(px, py), column24 ? end : origin) << int(turn) << " " << lx << "," << ly;
      }
    }
  }
}

// Column n's doses left and target, per logical pixel, before stage slot `j` (0..24, from 0) of `stage`: end-aligned,
// column n is driven before slot 24 - n, so a driven column has 24 - j doses left and an undriven one none, its target
// still the previous stage's end (stage 1: its origin). Gutters and the leftover hold the origin.
template <typename PixelFn>
void expectStageSlot(const TransitionScene& scene, const int lw, const int lh, const int j, const LadderStage stage,
                     PixelFn&& pixel, const char* what) {
  const int rows = scene.ladderHeight();
  const LadderLayout l = scene.ladderLayout();
  const int pitch = l.width + l.gutter;
  for (int ly = 0; ly < lh; ++ly) {
    const bool top = ly < rows / 2;
    const uint8_t origin = static_cast<uint8_t>(top ? Shade::W : Shade::K);
    const uint8_t opposite = static_cast<uint8_t>(top ? Shade::K : Shade::W);
    for (int lx = 0; lx < lw; ++lx) {
      const int n = lx / pitch + 1;
      const bool column = ly < rows && n <= l.columns && lx % pitch < l.width;
      const bool driven = column && n >= TransitionScene::LADDER_STEPS - j;
      const uint8_t stageEnd = stage == LadderStage::Ladder ? opposite : origin;
      const uint8_t before = stage == LadderStage::Ladder ? origin : opposite;
      uint8_t remaining = 0;
      uint8_t target = 0;
      pixel(lx, ly, remaining, target);
      ASSERT_EQ(remaining, driven && j < 24 ? 24 - j : 0) << what << " slot " << j << " at " << lx << "," << ly;
      ASSERT_EQ(target, column ? (driven ? stageEnd : before) : origin)
          << what << " slot " << j << " at " << lx << "," << ly;
    }
  }
}

// The stage's 24 slots (checked before each, and after the last), then the slot that drove none: its step.
template <typename PixelFn>
SceneStep runStage(TaskSim& sim, const int lw, const int lh, const LadderStage stage, PixelFn&& pixel,
                   const char* what) {
  for (int j = 0; j < TransitionScene::LADDER_STEPS; ++j) {
    EXPECT_TRUE(sim.scene.ladderPulsing()) << what << " " << j;
    expectStageSlot(sim.scene, lw, lh, j, stage, pixel, what);
    const SceneStep step = sim.slot();
    EXPECT_FALSE(step.ladderSettled.has_value()) << what << " " << j;
    EXPECT_FALSE(step.hold.has_value()) << what << " " << j;
  }
  expectStageSlot(sim.scene, lw, lh, TransitionScene::LADDER_STEPS, stage, pixel, what);
  return sim.slot();
}

// A Ladder run on a small screen: 72 px wide (24 columns of 2 px, 1 px gutters), 64 high (the marker's 16 rows under
// the halves, which share rows 0..47).
constexpr int LADDER_W = 72;
constexpr int LADDER_H = 64;

SceneStep runStage(TaskSim& sim, const LadderStage stage, const char* what) {
  return runStage(
      sim, LADDER_W, LADDER_H, stage,
      [&sim](const int x, const int y, uint8_t& remaining, uint8_t& target) {
        remaining = sim.b.model.remaining(x, y);
        target = sim.b.model.target(x, y);
      },
      what);
}

// The marker's slots until it settles (out: `held`; back: `resumed`); the last step.
SceneStep runMarker(TaskSim& sim, const bool out) {
  SceneStep step;
  for (int n = 0; n < 50 && !(out ? step.held : step.resumed); ++n) {
    EXPECT_FALSE(sim.scene.ladderPulsing());
    step = sim.slot();
    EXPECT_FALSE(step.ladderSettled.has_value());
  }
  EXPECT_TRUE(out ? step.held : step.resumed);
  return step;
}

SceneStep paintBase(TaskSim& sim) {
  SceneStep step;
  for (int n = 0; n < 50 && !step.ladderSettled; ++n) {
    EXPECT_FALSE(sim.scene.ladderPulsing());
    step = sim.slot();
  }
  EXPECT_TRUE(step.ladderSettled.has_value());
  if (step.ladderSettled) {
    EXPECT_EQ(step.ladderSettled->stage, LadderStage::Base);
  }
  return step;
}

// Both stages in every turn on a small panel: each logical pixel mapped as the renderer maps it.
TEST(TransitionScene, LadderStagesAreEndAlignedInEveryTurn) {
  constexpr int PW = 96;
  constexpr int PH = 72;
  for (const PanelTurn turn : TURNS) {
    const int lw = portrait(turn) ? PH : PW;
    const int lh = portrait(turn) ? PW : PH;
    TaskSim sim(turn, lw, lh, PW, PH, SweepMode::Ladder, 4, false);
    const auto pixel = [&sim, turn](const int lx, const int ly, uint8_t& remaining, uint8_t& target) {
      int px = 0;
      int py = 0;
      rendererRotate(turn, lx, ly, &px, &py, PW, PH);
      remaining = sim.b.model.remaining(px, py);
      target = sim.b.model.target(px, py);
    };
    paintBase(sim);
    SceneStep step = runStage(sim, lw, lh, LadderStage::Ladder, pixel, "stage 1");
    ASSERT_TRUE(step.hold.has_value()) << int(turn);
    runMarker(sim, true);
    ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    runMarker(sim, false);
    step = runStage(sim, lw, lh, LadderStage::Return, pixel, "stage 2");
    ASSERT_TRUE(step.hold.has_value()) << int(turn);
    EXPECT_EQ(step.hold->count, 2u);
  }
}

TEST(TransitionScene, LadderHoldsAfterEachStageAndEndsAfterTheReturn) {
  // Hold on and Hold off alike: the Ladder is nothing but its photos.
  for (const bool hold : {true, false}) {
    TaskSim sim(LADDER_W, LADDER_H, SweepMode::Ladder, 4, hold);
    ASSERT_EQ(sim.scene.ladderHeight(), 48);
    paintBase(sim);
    // Stage 1: column n has n pulses, one a slot, its last in the stage's last slot; the stage settles after 24 slots,
    // and the run holds at `ladder 1`.
    SceneStep step = runStage(sim, LadderStage::Ladder, "stage 1");
    ASSERT_TRUE(step.ladderSettled.has_value()) << hold;
    EXPECT_EQ(step.ladderSettled->stage, LadderStage::Ladder);
    EXPECT_EQ(step.ladderSettled->slots, 24u);
    ASSERT_TRUE(step.hold.has_value()) << hold;
    EXPECT_EQ(step.hold->spot, HoldSpot::Ladder);
    EXPECT_EQ(step.hold->count, 1u);
    EXPECT_FALSE(step.finished);
    EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerOut);
    EXPECT_EQ(sim.scene.markerShade(), Shade::L);  // over black, two steps along
    // Every column landed on its end in the model (top black, bottom white); the stage waits for the resume.
    EXPECT_EQ(sim.b.model.level(0, 0), 0);
    EXPECT_EQ(sim.b.model.level(69, 47), 192);
    EXPECT_EQ(sim.b.model.level(2, 0), 192);  // a gutter: its origin
    step = runMarker(sim, true);
    EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::Held);
    EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Ladder);
    // Next: the marker back first, then stage 2 starts in the slot it settles.
    ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    step = runMarker(sim, false);
    EXPECT_FALSE(step.finished);
    EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Return);
    EXPECT_EQ(sim.scene.markerShade(), Shade::K);
    // Stage 2: the same pulses back, end-aligned, then the hold at `ladder 2`.
    step = runStage(sim, LadderStage::Return, "stage 2");
    ASSERT_TRUE(step.ladderSettled.has_value());
    EXPECT_EQ(step.ladderSettled->stage, LadderStage::Return);
    EXPECT_EQ(step.ladderSettled->slots, 24u);
    ASSERT_TRUE(step.hold.has_value());
    EXPECT_EQ(step.hold->count, 2u);
    EXPECT_FALSE(step.finished);
    EXPECT_EQ(sim.b.model.level(0, 0), 192);
    EXPECT_EQ(sim.b.model.level(69, 47), 0);
    runMarker(sim, true);
    // Next: the marker back, then the run is over.
    ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
    step = runMarker(sim, false);
    EXPECT_TRUE(step.finished);
    EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Done);
    EXPECT_EQ(sim.b.model.activeCount(), 0u);
  }
}

TEST(TransitionScene, NarrowLadderStillRunsTwentyFourSlotsAStage) {
  // 20 px wide: 20 columns of 1 px; columns 21..24 do not exist, so the stage's first four slots drive nothing, and
  // still count.
  TaskSim sim(20, 64, SweepMode::Ladder, 4, true);
  paintBase(sim);
  SceneStep step;
  uint32_t slots = 0;
  while (!step.ladderSettled && slots < 50) {
    step = sim.slot();
    ++slots;
  }
  ASSERT_TRUE(step.ladderSettled.has_value());
  EXPECT_EQ(step.ladderSettled->slots, 24u);
  EXPECT_EQ(slots, 25u);
  ASSERT_TRUE(step.hold.has_value());
  EXPECT_EQ(sim.b.model.level(19, 0), 0);  // column 20 had its 20 pulses
}

TEST(TransitionScene, SessionCapCleanRestartsTheLadderFromTheBaseOrEndsItAfterBothPhotos) {
  // The cap's clean, as the task runs it: white, a hold dropped, the ladder restarted or ended, the scene reapplied.
  const auto capClean = [](TaskSim& sim, bool& ended) {
    sim.b.model.reset(Shade::W);
    const std::optional<DroppedHold> dropped = sim.scene.cancelHold();
    ended = sim.scene.ladderCleaned(dropped);
    sim.stale = true;
    return dropped;
  };
  bool ended = false;
  TaskSim sim(LADDER_W, LADDER_H, SweepMode::Ladder, 4, true);
  const SceneStep base = paintBase(sim);
  ASSERT_TRUE(base.ladderSettled.has_value());
  const uint32_t baseSlots = base.ladderSettled->slots;
  // Mid stage 1 (5 slots in): nothing to drop; the base paints again in as many slots, then stage 1 from its start.
  for (int n = 0; n < 5; ++n) sim.slot();
  EXPECT_FALSE(capClean(sim, ended).has_value());
  EXPECT_FALSE(ended);
  EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Base);
  EXPECT_FALSE(sim.scene.ladderPulsing());
  const SceneStep again = paintBase(sim);
  ASSERT_TRUE(again.ladderSettled.has_value());
  EXPECT_EQ(again.ladderSettled->slots, baseSlots);
  SceneStep step = runStage(sim, LadderStage::Ladder, "stage 1 after the cap");
  ASSERT_TRUE(step.hold.has_value());
  EXPECT_EQ(step.hold->count, 1u);
  // The marker going out at `ladder 1`: dropped, the ladder from the base, holding at `ladder 1` again.
  sim.slot();
  ASSERT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerOut);
  std::optional<DroppedHold> dropped = capClean(sim, ended);
  ASSERT_TRUE(dropped.has_value());
  EXPECT_FALSE(ended);
  EXPECT_EQ(dropped->spot, HoldSpot::Ladder);
  EXPECT_EQ(dropped->drop, HoldDrop::LadderFromBase);
  EXPECT_EQ(dropped->markerSlots, 1u);
  EXPECT_EQ(sim.scene.holdPhase(), HoldPhase::Running);
  paintBase(sim);
  step = runStage(sim, LadderStage::Ladder, "stage 1 after a dropped hold");
  ASSERT_TRUE(step.hold.has_value());
  EXPECT_EQ(step.hold->count, 1u);
  // The marker on its way back from `ladder 1`: the photo was taken, but stage 2's is not: from the base, and no
  // pending stage 2 survives the restart.
  runMarker(sim, true);
  ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  sim.slot();
  ASSERT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerBack);
  dropped = capClean(sim, ended);
  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->drop, HoldDrop::WayBackCutOff);
  EXPECT_FALSE(ended);
  step = paintBase(sim);
  EXPECT_FALSE(step.resumed);
  EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Ladder);
  step = runStage(sim, LadderStage::Ladder, "stage 1 after a cut-off way back");
  ASSERT_TRUE(step.hold.has_value());
  EXPECT_EQ(step.hold->count, 1u);
  // On to `ladder 2`, and the marker on its way back from it: both photos taken, so the run ends.
  runMarker(sim, true);
  ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  runMarker(sim, false);
  step = runStage(sim, LadderStage::Return, "stage 2 before the cap");
  ASSERT_TRUE(step.hold.has_value());
  EXPECT_EQ(step.hold->count, 2u);
  runMarker(sim, true);
  ASSERT_TRUE(sim.scene.resumeHold(sim.b.model, sim.b.preset));
  sim.slot();
  ASSERT_EQ(sim.scene.holdPhase(), HoldPhase::MarkerBack);
  dropped = capClean(sim, ended);
  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->drop, HoldDrop::WayBackCutOff);
  EXPECT_TRUE(ended);
  EXPECT_EQ(sim.scene.ladderStage(), LadderStage::Done);
}

}  // namespace

// The transition test's picture (lib/GameDisplayLab/TransitionScene): toPanel against a copy of
// GfxRenderer::rotateCoordinates for all four orientations, the bands and lanes, the sweep timing, update() touching
// only the rectangles' old and new areas (the plan's Transition sweep row, its pure part), and dlab-c3's modes: Slow's
// crossing from the slot period and the longest dose run, held off-screen until measured, Static's fixed rectangles
// and its settle-and-flip sequence through all twelve transitions, both decided by slotDone.

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
    scene.setMode(SweepMode::Fast, 380000, 12);  // the slot period and the dwell change nothing in Fast
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

TEST(TransitionScene, ModeRowCyclesStaticSlowFast) {
  EXPECT_EQ(nextMode(SweepMode::Static), SweepMode::Slow);
  EXPECT_EQ(nextMode(SweepMode::Slow), SweepMode::Fast);
  EXPECT_EQ(nextMode(SweepMode::Fast), SweepMode::Static);
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
      scene.setMode(SweepMode::Slow, periodUs, dwell);
      ASSERT_TRUE(scene.slotDone(1, true, periodUs, 0).measured);  // the crossing starts at clock 0
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
  capped.setMode(SweepMode::Slow, 600000, 12);
  EXPECT_EQ(capped.sweepMs(), TransitionScene::SLOW_SWEEP_MAX_MS);
  EXPECT_TRUE(capped.sweepCapped());
  EXPECT_EQ(capped.dwellSlotsAt(600000), 11u);  // 48 x 120,000 / 848 = 6,792 ms of 600 ms slots
}

TEST(TransitionScene, SlowWaitsOffScreenUntilTheFirstDeliveredSlotThenCrosses) {
  constexpr int W = 800;
  constexpr int HH = 480;
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.setMode(SweepMode::Slow, 100000, 7);
  EXPECT_EQ(scene.sweepMs(), 12367u);
  // Not measured yet: the rectangles stay just off the left edge whatever the clock says.
  for (const uint32_t ms : {0u, 400u, 5000u, 20000u}) EXPECT_EQ(scene.rectX(ms), -TransitionScene::RECT_W_SLOW) << ms;
  const int top = scene.rectTop(2, 0);
  EXPECT_EQ(scene.targetAt(0, top, 20000), Shade::L);  // no pixel under a rectangle
  // A slot whose drive scan was not delivered measures nothing.
  SceneStep step = scene.slotDone(500, false, 380000, 400);
  EXPECT_FALSE(step.measured);
  EXPECT_FALSE(step.reapply);
  EXPECT_EQ(scene.rectX(400), -TransitionScene::RECT_W_SLOW);
  // The first delivered slot sizes the crossing and starts it at its clock: nothing on screen moves, so no reapply.
  step = scene.slotDone(500, true, 380000, 400);
  EXPECT_TRUE(step.measured);
  EXPECT_FALSE(step.reapply);
  const uint32_t sweep = scene.sweepMs();
  EXPECT_EQ(sweep, TransitionScene::slowSweepMs(380000, W, 7));
  EXPECT_EQ(scene.rectX(400), -TransitionScene::RECT_W_SLOW);
  EXPECT_EQ(scene.rectX(400 + sweep / 2),
            -TransitionScene::RECT_W_SLOW + static_cast<int>(int64_t{W + 48} * (sweep / 2) / sweep));
  // Once per run.
  EXPECT_FALSE(scene.slotDone(500, true, 400000, 800).measured);
  EXPECT_EQ(scene.sweepMs(), sweep);
  // The model sees no jump: from the held position to the start of the crossing nothing is retargeted.
  {
    SceneBench b(96, HH);
    TransitionScene small(PanelTurn::LandscapeCounterClockwise, 96, HH, 96, HH);
    small.setMode(SweepMode::Slow, 100000, 7);
    small.applyAll(b.model, 300, b.preset);
    while (b.model.activeCount() > 0) b.model.commit(true, true, b.preset);
    const std::vector<uint8_t> settled = b.pixels;
    ASSERT_TRUE(small.slotDone(1, true, 380000, 400).measured);
    small.update(b.model, 300, 400, b.preset);
    EXPECT_EQ(b.pixels, settled);
  }
  // The next run starts held again and measures again; Fast never measures.
  scene.setMode(SweepMode::Slow, 380000, 7);
  EXPECT_EQ(scene.rectX(50000), -TransitionScene::RECT_W_SLOW);
  EXPECT_TRUE(scene.slotDone(1, true, 400000, 0).measured);
  EXPECT_EQ(scene.sweepMs(), TransitionScene::slowSweepMs(400000, W, 7));
  scene.setMode(SweepMode::Fast, 400000, 7);
  EXPECT_FALSE(scene.slotDone(1, true, 400000, 0).measured);
  EXPECT_EQ(scene.sweepMs(), TransitionScene::SWEEP_MS);
}

TEST(TransitionScene, StaticRectanglesStandStillInTheMiddleThird) {
  constexpr int W = 96;
  constexpr int HH = 480;
  SceneBench b(W, HH);
  TransitionScene scene(PanelTurn::LandscapeCounterClockwise, W, HH, W, HH);
  scene.setMode(SweepMode::Static, 380000, 4);
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
  scene.setMode(SweepMode::Static, 100000, b.preset.longestRunSlots());
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
    const SceneStep step = scene.slotDone(driven, true, 100000, 0);
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
  const SceneStep first = scene.slotDone(0, true, 100000, 0);
  ASSERT_TRUE(first.settled.has_value());
  EXPECT_EQ(first.settled->flip, 0u);
  EXPECT_EQ(first.settled->slots, 0u);
  EXPECT_EQ(scene.shift(), 1);
  // Not Static: no sequence.
  scene.setMode(SweepMode::Slow, 100000, 4);
  EXPECT_EQ(scene.shift(), 0);
  EXPECT_FALSE(scene.slotDone(0, true, 100000, 0).settled.has_value());
  EXPECT_EQ(scene.shift(), 0);
}

}  // namespace

// The gray viewer's picture (lib/GameDisplayLab/GrayPicture, story dlab-g3): the quantiser (plain and ordered at four
// and seven levels, D14), the test chart (deterministic, every level present), the run splitting, painted rows in the
// model in every orientation, the per-level counts, and the hold: paint, settle, the marker out to the extreme
// farther from the picture under it, held. Also the shared pause symbol (PanelGeometry, D15).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

#include "GrayModel.h"
#include "GrayPicture.h"
#include "GrayPreset.h"
#include "GraySlot.h"
#include "MaskPlanes.h"
#include "PanelGeometry.h"
#include "TransitionScene.h"

namespace {

using namespace GameDisplayLab;

constexpr PanelTurn TURNS[] = {PanelTurn::Portrait, PanelTurn::LandscapeClockwise, PanelTurn::PortraitInverted,
                               PanelTurn::LandscapeCounterClockwise};
constexpr uint8_t WHITE_LEVEL = SHADE_COUNT - 1;

bool portrait(const PanelTurn turn) { return turn == PanelTurn::Portrait || turn == PanelTurn::PortraitInverted; }

// A model over owned buffers on a panel pw x ph, its masks, and the logical screen for `turn`.
struct Bench {
  int pw;
  int ph;
  std::vector<uint8_t> pixels;
  std::vector<uint8_t> planes;
  GrayModel model;
  MaskPlanes drive;
  MaskPlanes follow;
  LogicalScreen screen;

  Bench(const PanelTurn turn, const int panelW, const int panelH)
      : pw(panelW),
        ph(panelH),
        pixels(GrayModel::bytesFor(panelW, panelH)),
        planes(4 * MaskPlanes::bytesFor(panelW, panelH)) {
    model = GrayModel(pixels.data(), static_cast<uint16_t>(pw), static_cast<uint16_t>(ph));
    model.reset(static_cast<Shade>(WHITE_LEVEL));
    const size_t n = MaskPlanes::bytesFor(pw, ph);
    drive = MaskPlanes(planes.data(), planes.data() + n, pw, ph);
    follow = MaskPlanes(planes.data() + 2 * n, planes.data() + 3 * n, pw, ph);
    screen = portrait(turn) ? LogicalScreen{turn, ph, pw, pw, ph} : LogicalScreen{turn, pw, ph, pw, ph};
  }

  // The target of logical pixel (lx, ly).
  uint8_t targetAt(const int lx, const int ly) const {
    int px = 0;
    int py = 0;
    toPanelPixel(screen.turn, lx, ly, pw, ph, px, py);
    return model.target(px, py);
  }

  // One slot with every scan delivered.
  SlotResult slot(const GrayPreset& preset) {
    return runGraySlot(model, drive, follow, preset, false, [](const MaskPlanes&, uint32_t& busyUs) {
      busyUs = 1000;
      return true;
    });
  }
};

// ---- quantiser ------------------------------------------------------------------------------------------------------

TEST(GrayQuantiser, LevelGraysAreEvenlySpaced) {
  EXPECT_EQ(levelGray(0, 4), 0);
  EXPECT_EQ(levelGray(1, 4), 85);
  EXPECT_EQ(levelGray(2, 4), 170);
  EXPECT_EQ(levelGray(3, 4), 255);
  const std::vector<uint8_t> seven = {levelGray(0, 7), levelGray(1, 7), levelGray(2, 7), levelGray(3, 7),
                                      levelGray(4, 7), levelGray(5, 7), levelGray(6, 7)};
  EXPECT_EQ(seven, (std::vector<uint8_t>{0, 43, 85, 128, 170, 213, 255}));
}

TEST(GrayQuantiser, PlainIsTheNearestLevel) {
  for (const uint8_t levels : {uint8_t{4}, uint8_t{7}}) {
    for (int g = 0; g <= 255; ++g) {
      const uint8_t q = quantiseLevel(static_cast<uint8_t>(g), 0, 0, Dither::Plain, levels);
      ASSERT_LT(q, levels);
      // No other level is nearer (ties go either way only at an exact midpoint).
      const int mine = std::abs(g * (levels - 1) - q * 255);
      for (int l = 0; l < levels; ++l) ASSERT_LE(mine, std::abs(g * (levels - 1) - l * 255)) << g << " " << l;
    }
  }
  EXPECT_EQ(quantiseLevel(42, 0, 0, Dither::Plain, 4), 0);
  EXPECT_EQ(quantiseLevel(43, 0, 0, Dither::Plain, 4), 1);
  EXPECT_EQ(quantiseLevel(128, 0, 0, Dither::Plain, 7), 3);
}

TEST(GrayQuantiser, EveryLevelsGrayStaysOnItsLevel) {
  for (const uint8_t levels : {uint8_t{2}, uint8_t{4}, uint8_t{7}}) {
    for (uint8_t l = 0; l < levels; ++l) {
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          ASSERT_EQ(quantiseLevel(levelGray(l, levels), x, y, Dither::Plain, levels), l);
          ASSERT_EQ(quantiseLevel(levelGray(l, levels), x, y, Dither::Ordered, levels), l) << int(levels);
        }
      }
    }
  }
}

TEST(GrayQuantiser, OrderedMixesTheTwoNeighboursInProportion) {
  for (const uint8_t levels : {uint8_t{4}, uint8_t{7}}) {
    for (int g = 0; g <= 255; ++g) {
      // Over one 4x4 tile only the two levels around g appear, and their mean is g's position within 1/16 of a step.
      int sum = 0;
      std::set<int> seen;
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          const int q = quantiseLevel(static_cast<uint8_t>(g), x, y, Dither::Ordered, levels);
          seen.insert(q);
          sum += q;
        }
      }
      ASSERT_LE(seen.size(), 2u);
      if (seen.size() == 2) {
        ASSERT_EQ(*seen.rbegin() - *seen.begin(), 1);
      }
      const double exact = g * (levels - 1) / 255.0;
      ASSERT_NEAR(sum / 16.0, exact, 1.0 / 16 + 1e-9) << int(levels) << " gray " << g;
    }
  }
}

TEST(GrayQuantiser, ExtremesAndMonotonicity) {
  for (const Dither d : {Dither::Plain, Dither::Ordered}) {
    for (const uint8_t levels : {uint8_t{4}, uint8_t{7}}) {
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          ASSERT_EQ(quantiseLevel(0, x, y, d, levels), 0);
          ASSERT_EQ(quantiseLevel(255, x, y, d, levels), levels - 1);
          for (int g = 1; g <= 255; ++g) {
            ASSERT_GE(quantiseLevel(static_cast<uint8_t>(g), x, y, d, levels),
                      quantiseLevel(static_cast<uint8_t>(g - 1), x, y, d, levels));
          }
        }
      }
    }
  }
}

TEST(GrayQuantiser, OrderedFollowsTheBayerTile) {
  // Halfway between level 0 and 1 of four (gray 43, a hair over half a step): the eight largest Bayer entries'
  // thresholds fall under it.
  int ones = 0;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) ones += quantiseLevel(43, x, y, Dither::Ordered, 4);
  }
  EXPECT_EQ(ones, 8);
  // The tile repeats every 4 px.
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      EXPECT_EQ(quantiseLevel(100, x, y, Dither::Ordered, 7), quantiseLevel(100, x & 3, y & 3, Dither::Ordered, 7));
    }
  }
}

TEST(GrayQuantiser, TheEnginesLevelCountIsUsed) {
  EXPECT_EQ(static_cast<uint8_t>(quantise(255, 0, 0, Dither::Plain)), SHADE_COUNT - 1);
  EXPECT_EQ(static_cast<uint8_t>(quantise(0, 0, 0, Dither::Ordered)), 0);
  for (int g = 0; g <= 255; g += 5) {
    EXPECT_EQ(static_cast<uint8_t>(quantise(static_cast<uint8_t>(g), 1, 2, Dither::Ordered)),
              quantiseLevel(static_cast<uint8_t>(g), 1, 2, Dither::Ordered, SHADE_COUNT));
  }
}

TEST(GrayLevels, TheReadoutNamesTheEnginesLevels) {
  char names[64];
  const size_t n = levelNames(names, sizeof(names));
  EXPECT_EQ(n, strlen(names));
  // SHADE_COUNT names, one character each, joined by ", ".
  EXPECT_EQ(n, static_cast<size_t>(SHADE_COUNT) * 3 - 2);
  EXPECT_EQ(names[0], shadeName(Shade::K));
  EXPECT_EQ(names[n - 1], shadeName(static_cast<Shade>(WHITE_LEVEL)));
  if (SHADE_COUNT == 4) {
    EXPECT_STREQ(names, "0, 2, 4, 6");
  }
  if (SHADE_COUNT == 7) {
    EXPECT_STREQ(names, "0, 1, 2, 3, 4, 5, 6");  // story dlab-g2: the viewer reads seven with no change of its own
  }
  // Cut short, still terminated.
  char small[3];
  EXPECT_EQ(levelNames(small, sizeof(small)), 2u);
  EXPECT_STREQ(small, "0,");
  EXPECT_EQ(levelNames(small, 0), 0u);
}

// ---- chart ----------------------------------------------------------------------------------------------------------

TEST(GrayChart, IsDeterministicAndKnown) {
  // The ramp's steps, the gradient's ends, a hatch pixel, white elsewhere.
  EXPECT_EQ(testChartGray(0, 0, 480, 800, 4), 0);
  EXPECT_EQ(testChartGray(479, 0, 480, 800, 4), 255);
  EXPECT_EQ(testChartGray(130, 10, 480, 800, 4), 85);
  EXPECT_EQ(testChartGray(0, 200, 480, 800, 4), 0);
  EXPECT_EQ(testChartGray(479, 200, 480, 800, 4), 255);
  EXPECT_EQ(testChartGray(240, 200, 480, 800, 4), (240 * 255 + 239) / 479);
  EXPECT_EQ(testChartGray(0, 330, 480, 800, 4), 0);    // (0 + 330) % 3 == 0
  EXPECT_EQ(testChartGray(1, 330, 480, 800, 4), 255);  // 331 % 3 != 0
  // Off-screen reads as white.
  EXPECT_EQ(testChartGray(-1, 0, 480, 800, 4), 255);
  EXPECT_EQ(testChartGray(0, 800, 480, 800, 4), 255);
  // The same pixel twice is the same gray.
  for (int y = 0; y < 800; y += 37) {
    for (int x = 0; x < 480; x += 29) EXPECT_EQ(testChartGray(x, y, 480, 800, 7), testChartGray(x, y, 480, 800, 7));
  }
}

TEST(GrayChart, EveryLevelIsPresentUnderBothDithers) {
  for (const uint8_t levels : {uint8_t{4}, uint8_t{7}}) {
    for (const auto& size : {std::array<int, 2>{480, 800}, std::array<int, 2>{800, 480}, std::array<int, 2>{40, 30}}) {
      for (const Dither d : {Dither::Plain, Dither::Ordered}) {
        std::set<int> seen;
        for (int y = 0; y < size[1]; ++y) {
          for (int x = 0; x < size[0]; ++x) {
            seen.insert(quantiseLevel(testChartGray(x, y, size[0], size[1], levels), x, y, d, levels));
          }
        }
        EXPECT_EQ(seen.size(), levels) << size[0] << "x" << size[1] << " dither " << int(d);
      }
    }
  }
}

TEST(GrayChart, LineArtIsBlackOnWhite) {
  // Everything below the gradient is pure black or white: plain and ordered paint it the same.
  for (int y = 320; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      const uint8_t g = testChartGray(x, y, 480, 800, 7);
      ASSERT_TRUE(g == 0 || g == 255) << x << "," << y;
    }
  }
  // Each part has some ink: hatching in each third, rings, the four strokes.
  int hatch[3] = {};
  int rings = 0;
  int strokes = 0;
  for (int y = 320; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      if (testChartGray(x, y, 480, 800, 4) != 0) continue;
      if (y < 480) {
        ++hatch[x * 3 / 480];
      } else if (x < 240) {
        ++rings;
      } else {
        ++strokes;
      }
    }
  }
  EXPECT_GT(hatch[0], hatch[1]);  // pitch 3 is denser than 6, denser than 12
  EXPECT_GT(hatch[1], hatch[2]);
  EXPECT_GT(rings, 0);
  EXPECT_GT(strokes, 0);
}

// ---- runs and painting ----------------------------------------------------------------------------------------------

TEST(GrayRuns, SplitsEqualNeighbours) {
  const uint8_t values[] = {1, 1, 2, 3, 3, 3, 1};
  std::vector<std::array<int, 3>> runs;
  forEachRun(values, 7,
             [&](const int start, const int length, const uint8_t v) { runs.push_back({start, length, v}); });
  EXPECT_EQ(runs, (std::vector<std::array<int, 3>>{{0, 2, 1}, {2, 1, 2}, {3, 3, 3}, {6, 1, 1}}));
  runs.clear();
  forEachRun(values, 0, [&](int, int, uint8_t) { runs.push_back({}); });
  EXPECT_TRUE(runs.empty());
  const uint8_t same[] = {4, 4, 4};
  forEachRun(same, 3, [&](const int start, const int length, const uint8_t v) { runs.push_back({start, length, v}); });
  EXPECT_EQ(runs, (std::vector<std::array<int, 3>>{{0, 3, 4}}));
}

TEST(GrayPaint, RowsLandOnTheirLogicalPixelsInEveryTurn) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  for (const PanelTurn turn : TURNS) {
    Bench b(turn, 24, 12);
    const GrayPicture picture(b.screen);
    const int lw = b.screen.logicalW;
    std::vector<uint8_t> gray(static_cast<size_t>(lw));
    std::vector<uint8_t> shades(static_cast<size_t>(lw));
    for (int x = 0; x < lw; ++x) gray[static_cast<size_t>(x)] = static_cast<uint8_t>(x * 255 / (lw - 1));
    // Row 3 from column 2, all but the last two columns.
    picture.paintGrayRow(b.model, preset, 3, 2, gray.data(), lw - 4, Dither::Plain, shades.data());
    for (int ly = 0; ly < b.screen.logicalH; ++ly) {
      for (int lx = 0; lx < lw; ++lx) {
        const bool painted = ly == 3 && lx >= 2 && lx < lw - 2;
        const uint8_t want =
            painted ? static_cast<uint8_t>(quantise(gray[static_cast<size_t>(lx - 2)], lx, 3, Dither::Plain))
                    : WHITE_LEVEL;
        ASSERT_EQ(b.targetAt(lx, ly), want) << int(turn) << " " << lx << "," << ly;
      }
    }
  }
}

TEST(GrayPaint, RowsClipToTheScreen) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  Bench b(PanelTurn::LandscapeCounterClockwise, 8, 4);
  const GrayPicture picture(b.screen);
  const std::vector<uint8_t> black(12, 0);
  std::vector<uint8_t> shades(12);
  picture.paintGrayRow(b.model, preset, 1, -2, black.data(), 12, Dither::Plain, shades.data());
  picture.paintGrayRow(b.model, preset, 9, 0, black.data(), 8, Dither::Plain, shades.data());  // below: nothing
  for (int x = 0; x < 8; ++x) {
    EXPECT_EQ(b.targetAt(x, 1), 0);
    EXPECT_EQ(b.targetAt(x, 0), WHITE_LEVEL);
    EXPECT_EQ(b.targetAt(x, 3), WHITE_LEVEL);
  }
}

TEST(GrayPaint, TheChartIsTheQuantisedChart) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  for (const PanelTurn turn : {PanelTurn::Portrait, PanelTurn::LandscapeCounterClockwise}) {
    for (const Dither d : {Dither::Plain, Dither::Ordered}) {
      Bench b(turn, 60, 40);
      const GrayPicture picture(b.screen);
      std::vector<uint8_t> gray(static_cast<size_t>(b.screen.logicalW));
      std::vector<uint8_t> shades(static_cast<size_t>(b.screen.logicalW));
      picture.paintChart(b.model, preset, d, gray.data(), shades.data());
      const int lw = b.screen.logicalW;
      const int lh = b.screen.logicalH;
      for (int ly = 0; ly < lh; ++ly) {
        for (int lx = 0; lx < lw; ++lx) {
          ASSERT_EQ(b.targetAt(lx, ly),
                    static_cast<uint8_t>(quantise(testChartGray(lx, ly, lw, lh, SHADE_COUNT), lx, ly, d)));
        }
      }
      // Every level is somewhere in the model, and the counts add up to the screen.
      const auto counts = levelCounts(b.model);
      uint32_t total = 0;
      for (const uint32_t c : counts) {
        EXPECT_GT(c, 0u);
        total += c;
      }
      EXPECT_EQ(total, static_cast<uint32_t>(lw * lh));
    }
  }
}

// ---- the hold -------------------------------------------------------------------------------------------------------

TEST(GrayPictureHold, PaintSettlesThenTheMarkerGoesOutThenHolds) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  Bench b(PanelTurn::Portrait, 80, 48);  // logical 48 x 80
  GrayPicture picture(b.screen);
  picture.restart();
  const std::vector<uint8_t> black(48, 0);
  std::vector<uint8_t> shades(48);
  for (int ly = 0; ly < 80; ++ly) {
    picture.paintGrayRow(b.model, preset, ly, 0, black.data(), 48, Dither::Plain, shades.data());
  }
  uint32_t paintSlots = 0;
  std::optional<uint32_t> painted;
  for (int i = 0; i < 100 && !painted; ++i) {
    const SlotResult slot = b.slot(preset);
    if (slot.driven > 0) ++paintSlots;
    const PictureStep step = picture.slotDone(b.model, preset, slot.driven);
    EXPECT_FALSE(step.held);
    painted = step.painted;
  }
  ASSERT_TRUE(painted);
  EXPECT_EQ(*painted, paintSlots);
  EXPECT_GT(paintSlots, 0u);
  EXPECT_EQ(picture.phase(), PicturePhase::MarkerOut);
  // A black picture: the marker goes white.
  EXPECT_EQ(static_cast<uint8_t>(picture.markerShade()), WHITE_LEVEL);
  const auto bars = picture.markerBars();
  EXPECT_EQ(bars[1].x + bars[1].w, 48);  // the logical right edge
  EXPECT_EQ(bars[0].y, 80 - GrayPicture::MARKER_PX);
  EXPECT_EQ(b.targetAt(bars[0].x, 79), WHITE_LEVEL);
  EXPECT_EQ(b.targetAt(bars[1].x, 64), WHITE_LEVEL);
  // The gap stays black.
  EXPECT_EQ(b.targetAt(bars[0].x + bars[0].w, 79), 0);
  bool held = false;
  uint32_t markerSlots = 0;
  for (int i = 0; i < 100 && !held; ++i) {
    const SlotResult slot = b.slot(preset);
    const PictureStep step = picture.slotDone(b.model, preset, slot.driven);
    held = step.held;
    markerSlots = step.markerSlots;
    EXPECT_FALSE(step.painted);
  }
  ASSERT_TRUE(held);
  EXPECT_GT(markerSlots, 0u);
  EXPECT_EQ(picture.phase(), PicturePhase::Held);
  EXPECT_EQ(b.model.activeCount(), 0u);
  // Held: nothing more.
  const PictureStep after = picture.slotDone(b.model, preset, 0);
  EXPECT_FALSE(after.held);
  EXPECT_FALSE(after.painted);
  // A restart (Next, or the session cap's clean) paints again from Painting.
  picture.restart();
  EXPECT_EQ(picture.phase(), PicturePhase::Painting);
}

TEST(GrayPictureHold, ASessionCapCleanMidMarkerPaintsAgain) {
  // The I/O matrix's session-cap row: the runner's clean resets the model to white, the viewer restarts its picture
  // (dropping the marker on its way out) and paints the entry again, which settles and holds as before.
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  Bench b(PanelTurn::LandscapeCounterClockwise, 40, 24);
  GrayPicture picture(b.screen);
  std::vector<uint8_t> gray(40, 0);
  std::vector<uint8_t> shades(40);
  const auto paint = [&]() {
    for (int ly = 0; ly < 24; ++ly)
      picture.paintGrayRow(b.model, preset, ly, 0, gray.data(), 40, Dither::Plain, shades.data());
  };
  picture.restart();
  paint();
  for (int i = 0; i < 100 && picture.phase() == PicturePhase::Painting; ++i) {
    picture.slotDone(b.model, preset, b.slot(preset).driven);
  }
  ASSERT_EQ(picture.phase(), PicturePhase::MarkerOut);
  // The cap's clean.
  b.model.reset(static_cast<Shade>(WHITE_LEVEL));
  picture.restart();
  EXPECT_EQ(picture.phase(), PicturePhase::Painting);
  paint();
  bool held = false;
  std::optional<uint32_t> painted;
  for (int i = 0; i < 200 && !held; ++i) {
    const PictureStep step = picture.slotDone(b.model, preset, b.slot(preset).driven);
    if (step.painted) painted = step.painted;
    held = step.held;
  }
  EXPECT_TRUE(painted);
  EXPECT_TRUE(held);
}

TEST(GrayPictureHold, MarkerGoesToTheFartherExtreme) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  Bench b(PanelTurn::LandscapeCounterClockwise, 64, 32);
  GrayPicture picture(b.screen);
  // White under the bars: black.
  EXPECT_EQ(picture.markerShadeFor(b.model), Shade::K);
  // Black under them: white.
  b.model.reset(Shade::K);
  EXPECT_EQ(static_cast<uint8_t>(picture.markerShadeFor(b.model)), WHITE_LEVEL);
  // The exact middle (each bar's top half black, its bottom half white): black; one row darker: white.
  b.model.reset(static_cast<Shade>(WHITE_LEVEL));
  const auto bars = picture.markerBars();
  GrayModel& m = b.model;
  for (const ScreenRect& bar : bars) {
    // Logical and panel coordinates agree in LandscapeCounterClockwise; settled by running slots below.
    for (int y = bar.y; y < bar.y + bar.h / 2; ++y) {
      for (int x = bar.x; x < bar.x + bar.w; ++x) m.retarget(x, y, Shade::K, preset);
    }
  }
  for (int i = 0; i < 40 && b.model.activeCount() > 0; ++i) b.slot(preset);
  EXPECT_EQ(picture.markerShadeFor(b.model), Shade::K);  // the tie goes to black
  for (int x = bars[0].x; x < bars[0].x + bars[0].w; ++x) m.retarget(x, bars[0].y + bars[0].h / 2, Shade::K, preset);
  for (int i = 0; i < 40 && b.model.activeCount() > 0; ++i) b.slot(preset);
  EXPECT_EQ(static_cast<uint8_t>(picture.markerShadeFor(b.model)), WHITE_LEVEL);
}

TEST(GrayPictureHold, MarkerSitsAtTheBottomRightAndClips) {
  Bench b(PanelTurn::LandscapeCounterClockwise, 800, 480);
  const GrayPicture picture(b.screen);
  const ScreenRect m = picture.markerRect();
  EXPECT_EQ(m.x, 800 - GrayPicture::MARKER_W);
  EXPECT_EQ(m.y, 480 - GrayPicture::MARKER_PX);
  EXPECT_EQ(m.w, GrayPicture::MARKER_W);
  EXPECT_EQ(m.h, GrayPicture::MARKER_PX);
  Bench tiny(PanelTurn::LandscapeCounterClockwise, 10, 4);
  const GrayPicture small(tiny.screen);
  const ScreenRect s = small.markerRect();
  EXPECT_EQ(s.x, 0);
  EXPECT_EQ(s.y, 0);
  EXPECT_EQ(s.w, 10);
  EXPECT_EQ(s.h, 4);
}

// ---- the shared pause symbol ----------------------------------------------------------------------------------------

TEST(PauseBars, TwoBarsAndAGap) {
  const auto bars = pauseBars(ScreenRect{100, 50, 32, 16}, 8);
  EXPECT_EQ(bars[0].x, 100);
  EXPECT_EQ(bars[0].w, 12);
  EXPECT_EQ(bars[1].x, 120);
  EXPECT_EQ(bars[1].w, 12);
  EXPECT_EQ(bars[0].h, 16);
  EXPECT_EQ(bars[1].y, 50);
  // Too narrow: the whole box and an empty bar.
  const auto one = pauseBars(ScreenRect{0, 0, 9, 4}, 8);
  EXPECT_EQ(one[0].w, 9);
  EXPECT_EQ(one[1].w, 0);
}

TEST(PauseBars, SettledOnlyWhenNoBarPixelHasADoseLeft) {
  const GrayPreset preset = GrayPreset::inkBoyDefaults();
  Bench b(PanelTurn::Portrait, 40, 20);
  const auto bars = pauseBars(ScreenRect{0, 0, 20, 4}, 8);
  EXPECT_TRUE(barsSettled(b.model, bars, b.screen));
  // A pixel in the gap moving does not count.
  retargetLogicalRect(b.model, b.screen, 7, 8, 0, 1, Shade::K, preset);
  EXPECT_TRUE(barsSettled(b.model, bars, b.screen));
  retargetLogicalRect(b.model, b.screen, 19, 20, 3, 1, Shade::K, preset);
  EXPECT_FALSE(barsSettled(b.model, bars, b.screen));
}

}  // namespace

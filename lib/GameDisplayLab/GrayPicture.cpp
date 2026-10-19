#include "GrayPicture.h"

#include "TransitionScene.h"  // shadeName

namespace GameDisplayLab {

namespace {

// The 4x4 Bayer matrix (the converters' DitherUtils.h bayer4x4, the same order).
constexpr uint8_t BAYER_4X4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// The chart's hatch pitches, left to right, and its stroke widths.
constexpr int HATCH_PITCHES[3] = {3, 6, 12};
constexpr int STROKE_WIDTHS[4] = {1, 2, 4, 8};
constexpr int RING_PITCH = 16;
constexpr int RING_WIDTH = 2;
constexpr uint8_t BLACK = 0;
constexpr uint8_t WHITE = 255;

// floor(sqrt(v)) for v >= 0.
int isqrt(const int64_t v) {
  if (v <= 0) return 0;
  int64_t r = 0;
  int64_t bit = int64_t{1} << 62;
  int64_t n = v;
  while (bit > n) bit >>= 2;
  while (bit != 0) {
    if (n >= r + bit) {
      n -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<int>(r);
}

}  // namespace

uint8_t levelGray(const uint8_t level, const uint8_t levels) {
  const unsigned top = levels > 1 ? levels - 1u : 1u;
  const unsigned l = level < top ? level : top;
  return static_cast<uint8_t>((l * 255u + top / 2) / top);
}

uint8_t quantiseLevel(const uint8_t gray, const int x, const int y, const Dither dither, const uint8_t levels) {
  const unsigned top = levels > 1 ? levels - 1u : 1u;
  unsigned level = 0;
  if (dither == Dither::Plain) {
    level = (gray * top + 127u) / 255u;
  } else {
    // Thresholds at (2b + 1) / 32 of a step, b the Bayer entry: the 16 thresholds sit evenly inside every step.
    const unsigned b = BAYER_4X4[y & 3][x & 3];
    level = (gray * top * 32u + (2u * b + 1u) * 255u) / (255u * 32u);
  }
  return static_cast<uint8_t>(level < top ? level : top);
}

Shade quantise(const uint8_t gray, const int x, const int y, const Dither dither) {
  return static_cast<Shade>(quantiseLevel(gray, x, y, dither, SHADE_COUNT));
}

uint8_t testChartGray(const int x, const int y, const int w, const int h, const uint8_t levels) {
  if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h) return WHITE;
  int band = static_cast<int>(static_cast<int64_t>(y) * 5 / h);
  if (band > 4) band = 4;
  switch (band) {
    case 0: {
      // The engine's levels, black to white, in equal steps.
      const int step = static_cast<int>(static_cast<int64_t>(x) * levels / w);
      return levelGray(static_cast<uint8_t>(step), levels);
    }
    case 1:
      // A continuous gradient.
      return w > 1 ? static_cast<uint8_t>((x * 255 + (w - 1) / 2) / (w - 1)) : BLACK;
    case 2: {
      // Hatching at three pitches.
      int third = static_cast<int>(static_cast<int64_t>(x) * 3 / w);
      if (third > 2) third = 2;
      return (x + y) % HATCH_PITCHES[third] == 0 ? BLACK : WHITE;
    }
    default:
      break;
  }
  // The last two fifths: rings in the left half, strokes in the right.
  const int top = static_cast<int>(static_cast<int64_t>(h) * 3 / 5);
  const int half = w / 2;
  if (x < half) {
    const int cx = half / 2;
    const int cy = top + (h - top) / 2;
    const int64_t dx = x - cx;
    const int64_t dy = y - cy;
    return isqrt(dx * dx + dy * dy) % RING_PITCH < RING_WIDTH ? BLACK : WHITE;
  }
  // Each stroke is the band c <= u + v < c + width along the anti-diagonal, its widths spread evenly over the area.
  const int u = x - half;
  const int v = y - top;
  const int span = (w - half) + (h - top);
  for (int k = 0; k < 4; ++k) {
    const int c = (k + 1) * span / 5;
    if (u + v >= c && u + v < c + STROKE_WIDTHS[k]) return BLACK;
  }
  return WHITE;
}

size_t levelNames(char* out, const size_t size) {
  if (size == 0) return 0;
  size_t used = 0;
  for (uint8_t level = 0; level < SHADE_COUNT; ++level) {
    const char name[4] = {level > 0 ? ',' : '\0', ' ', shadeName(static_cast<Shade>(level)), '\0'};
    const char* part = level > 0 ? name : name + 2;
    for (size_t i = 0; part[i] != '\0'; ++i) {
      if (used + 1 >= size) {
        out[used] = '\0';
        return used;
      }
      out[used++] = part[i];
    }
  }
  out[used] = '\0';
  return used;
}

std::array<uint32_t, SHADE_COUNT> levelCounts(const GrayModel& model) {
  std::array<uint32_t, SHADE_COUNT> counts{};
  for (int y = 0; y < model.height(); ++y) {
    for (int x = 0; x < model.width(); ++x) {
      const uint8_t t = model.target(x, y);
      if (t < SHADE_COUNT) ++counts[t];
    }
  }
  return counts;
}

void GrayPicture::paintGrayRow(GrayModel& model, const GrayPreset& preset, const int ly, const int x0,
                               const uint8_t* gray, const int count, const Dither dither, uint8_t* shades) const {
  for (int i = 0; i < count; ++i) shades[i] = static_cast<uint8_t>(quantise(gray[i], x0 + i, ly, dither));
  forEachRun(shades, count, [&](const int start, const int length, const uint8_t shade) {
    retargetLogicalRect(model, screen_, x0 + start, x0 + start + length, ly, 1, static_cast<Shade>(shade), preset);
  });
}

void GrayPicture::paintChart(GrayModel& model, const GrayPreset& preset, const Dither dither, uint8_t* gray,
                             uint8_t* shades) const {
  for (int ly = 0; ly < screen_.logicalH; ++ly) {
    for (int lx = 0; lx < screen_.logicalW; ++lx) {
      gray[lx] = testChartGray(lx, ly, screen_.logicalW, screen_.logicalH, SHADE_COUNT);
    }
    paintGrayRow(model, preset, ly, 0, gray, screen_.logicalW, dither, shades);
  }
}

void GrayPicture::restart() {
  phase_ = PicturePhase::Painting;
  paintSlots_ = 0;
  markerSlots_ = 0;
  markerShade_ = Shade::K;
}

ScreenRect GrayPicture::markerRect() const {
  const int w = screen_.logicalW < MARKER_W ? screen_.logicalW : MARKER_W;
  const int h = screen_.logicalH < MARKER_PX ? screen_.logicalH : MARKER_PX;
  return ScreenRect{screen_.logicalW - w, screen_.logicalH - h, w, h};
}

Shade GrayPicture::markerShadeFor(const GrayModel& model) const {
  // The mean level under the bars, as a sum against the midpoint: at or above it white is nearer, so black is farther.
  constexpr uint32_t TOP_LEVEL = static_cast<uint32_t>(SHADE_COUNT - 1) * GrayModel::LEVEL_STEP;
  uint64_t sum = 0;
  uint64_t count = 0;
  for (const ScreenRect& logical : markerBars()) {
    if (logical.w <= 0 || logical.h <= 0) continue;
    const ScreenRect panel = toPanel(screen_.turn, logical, screen_.panelW, screen_.panelH);
    for (int y = panel.y; y < panel.y + panel.h; ++y) {
      for (int x = panel.x; x < panel.x + panel.w; ++x) {
        sum += model.level(x, y);
        ++count;
      }
    }
  }
  return 2 * sum >= TOP_LEVEL * count ? Shade::K : static_cast<Shade>(SHADE_COUNT - 1);
}

PictureStep GrayPicture::slotDone(GrayModel& model, const GrayPreset& preset, const size_t driven) {
  PictureStep step;
  switch (phase_) {
    case PicturePhase::Held:
      return step;  // the task runs no slot while the run holds
    case PicturePhase::MarkerOut:
      ++markerSlots_;
      if (!barsSettled(model, markerBars(), screen_)) return step;
      phase_ = PicturePhase::Held;
      step.held = true;
      step.markerSlots = markerSlots_;
      return step;
    case PicturePhase::Painting:
      break;
  }
  if (driven > 0) {
    ++paintSlots_;
    return step;
  }
  // No pixel had a dose left for this slot: the picture settled. The marker goes out over it.
  step.painted = paintSlots_;
  phase_ = PicturePhase::MarkerOut;
  markerSlots_ = 0;
  markerShade_ = markerShadeFor(model);
  for (const ScreenRect& bar : markerBars()) {
    retargetLogicalRect(model, screen_, bar.x, bar.x + bar.w, bar.y, bar.h, markerShade_, preset);
  }
  return step;
}

}  // namespace GameDisplayLab

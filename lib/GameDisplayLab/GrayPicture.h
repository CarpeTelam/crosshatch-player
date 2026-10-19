#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "GrayModel.h"
#include "GrayPreset.h"
#include "PanelGeometry.h"

// The gray viewer's picture (story dlab-g3, the epic's D16): 8-bit gray quantised to the engine's levels and painted
// into the model, the built-in test chart, and the viewer's hold for the photo. Pure C++, pinned by
// test_gray_picture.cpp. The level count is the engine's (SHADE_COUNT): nothing here names a level count, so dlab-g2's
// seven levels need no change; black is level 0 and white SHADE_COUNT - 1.
//
// A run (GrayViewerActivity on the shared GrayRunner) cleans the panel to white, paints the picture (paintChart, or
// paintGrayRow for each row the reader decodes), then reports each slot to slotDone: the first slot that drove no
// pixel means the paint settled (`painted`, with its slot count); the hold marker (a pause symbol, PanelGeometry's
// pauseBars, MARKER_W x MARKER_PX at the logical bottom-right) is then driven to black or white, whichever is farther
// from the mean model level under its bars (a tie goes to black), and once its bars have settled the run holds
// (`held`). The marker is never driven back: the viewer's Next cleans the panel before the next picture.
namespace GameDisplayLab {

// The Dither row: plain (the nearest level) or ordered (the 4x4 Bayer matrix, scaled to the level count).
enum class Dither : uint8_t { Plain, Ordered };

// The gray (0 black .. 255 white) level `level` of `levels` stands for: round(level * 255 / (levels - 1)); levels >= 2.
uint8_t levelGray(uint8_t level, uint8_t levels);
// `gray` at logical (x, y) quantised to one of `levels` levels (>= 2): Plain, the nearest, (gray * (levels - 1) + 127)
// / 255; Ordered, (gray * (levels - 1) * 32 + (2b + 1) * 255) / (255 * 32) with b the Bayer 4x4 entry at (x & 3,
// y & 3), at most levels - 1. A level's own gray (levelGray) quantises to that level under both.
uint8_t quantiseLevel(uint8_t gray, int x, int y, Dither dither, uint8_t levels);
// quantiseLevel at the engine's level count, as a shade.
Shade quantise(uint8_t gray, int x, int y, Dither dither);

// The built-in test chart's gray at logical (x, y) of a w x h screen, deterministic, top to bottom in fifths of the
// height: the engine's levels as `levels` equal steps left to right (levelGray); a continuous gradient, 0 at the left
// to 255 at the right; hatching, black 1 px diagonals on white at pitches 3, 6 and 12 px in the left, middle and right
// thirds; in the last two fifths, concentric black rings 2 px wide every 16 px in the left half (centred in it), and
// black 45-degree strokes 1, 2, 4 and 8 px wide on white in the right half. Every gray is 0, 255 or a level's or the
// gradient's.
uint8_t testChartGray(int x, int y, int w, int h, uint8_t levels);

// The engine's levels by name (shadeName, D15's numbers), black first, joined by ", " ("0, 1, .., 6") into `out`
// (NUL-terminated, cut short when `size` is too small); returns the characters written.
size_t levelNames(char* out, size_t size);

// Calls fn(start, length, value) for each run of equal values in values[0, count), left to right.
template <typename Fn>
void forEachRun(const uint8_t* values, const int count, Fn&& fn) {
  int start = 0;
  while (start < count) {
    int end = start + 1;
    while (end < count && values[end] == values[start]) ++end;
    fn(start, end - start, values[start]);
    start = end;
  }
}

// Pixels per target level over the whole model, black first: index i counts targets i (i < SHADE_COUNT).
std::array<uint32_t, SHADE_COUNT> levelCounts(const GrayModel& model);

// Where the viewer's run is.
enum class PicturePhase : uint8_t {
  // The picture's pixels are being driven to their targets.
  Painting,
  // The paint settled: the marker is being driven out.
  MarkerOut,
  // The marker settled: the run holds until Next (the next picture) or a stop.
  Held,
};

// What a slot left for the viewer (GrayPicture::slotDone).
struct PictureStep {
  // The paint settled in this slot after this many slots that drove pixels; the marker goes out now.
  std::optional<uint32_t> painted;
  // The marker settled in this slot after markerSlots slots: the run holds.
  bool held = false;
  uint32_t markerSlots = 0;
};

class GrayPicture {
 public:
  // The marker's box: as wide as the transition test's and as high as its Ladder's (a pause symbol, D15).
  static constexpr int MARKER_W = 32;
  static constexpr int MARKER_PX = 16;
  static constexpr int MARKER_GAP = 8;

  GrayPicture() = default;
  explicit GrayPicture(const LogicalScreen& screen) : screen_(screen) {}

  const LogicalScreen& screen() const { return screen_; }

  // Quantises grays gray[0, count) at logical row `ly` from column `x0` with `dither` into shades[0, count) (count
  // bytes), then retargets each run of equal shades as one logical rectangle (clipped to the screen).
  void paintGrayRow(GrayModel& model, const GrayPreset& preset, int ly, int x0, const uint8_t* gray, int count,
                    Dither dither, uint8_t* shades) const;
  // The test chart over the whole logical screen, row by row; `gray` and `shades` hold a logical row each.
  void paintChart(GrayModel& model, const GrayPreset& preset, Dither dither, uint8_t* gray, uint8_t* shades) const;

  // A new paint: Painting, no slots counted, the marker unset (a run's start, the next picture, a session-cap clean).
  void restart();
  PicturePhase phase() const { return phase_; }
  // After each slot that drove `driven` pixels, `model` committed: Painting until a slot drives none (painted, the
  // marker retargeted with `preset`), then MarkerOut until the marker's bars have settled (held). Nothing while Held.
  PictureStep slotDone(GrayModel& model, const GrayPreset& preset, size_t driven);

  // The marker's logical box: MARKER_W x MARKER_PX at the bottom-right, clipped to a smaller screen.
  ScreenRect markerRect() const;
  std::array<ScreenRect, 2> markerBars() const { return pauseBars(markerRect(), MARKER_GAP); }
  // The shade the marker is driven to (black or white, farther from the mean level under its bars in `model`).
  Shade markerShadeFor(const GrayModel& model) const;
  // The shade the marker was driven to (valid from `painted` on).
  Shade markerShade() const { return markerShade_; }

 private:
  LogicalScreen screen_{PanelTurn::LandscapeCounterClockwise, 0, 0, 0, 0};
  PicturePhase phase_ = PicturePhase::Painting;
  uint32_t paintSlots_ = 0;
  uint32_t markerSlots_ = 0;
  Shade markerShade_ = Shade::K;
};

}  // namespace GameDisplayLab

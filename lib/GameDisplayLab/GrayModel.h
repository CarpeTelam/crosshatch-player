#pragma once

#include <cstddef>
#include <cstdint>

#include "GrayPreset.h"
#include "MaskPlanes.h"

// The gray engine's per-pixel model (the display-lab epic, R3): InkBoy's design (the D2 round 2 digest, Q2-Q5) over
// the lab's full-screen masks. Pure C++, pinned by test/game_display_lab/test_gray_model.cpp.
//
// A non-owning view over caller buffers in panel coordinates (the masks' pixel order), each pixel PIXEL_BYTES bytes:
// its modelled level (0..192, shade * 64; K = 0, W = 192), the doses still to deliver (0..DOSE_MAX), then target |
// source << 2 (the shade it moves to; the shade nearest its level at the last retarget, which picks the hold bit).
// The doses-left count has a byte of its own since the cap rose to 24 (the epic's D12), past the four bits it shared
// with the two shades.
// Optional: a history array of three bytes per pixel (origin shade, or ORIGIN_NONE; the doses delivered darker and
// lighter since the last retarget), and a weak bitmap (one bit per pixel, row-major, most significant bit first: a W >
// L pixel that darkens through the weak row). Without them (null) history routes and weak mode are off.
//
// One slot (GraySlot.h): buildMasks puts every pixel with doses left in the drive mask toward its target, and in the
// follow-up mask too when its transition's hold bit is off and it has two or more doses left; commit then advances
// each driven pixel one dose (two for follow-up pixels when the follow-up scan was delivered too): level +=
// (target * 64 - level) / remaining, remaining - 1, so the last dose lands on the target exactly. A scan that was not
// delivered commits nothing.
namespace GameDisplayLab {

class GrayModel {
 public:
  static constexpr uint8_t LEVEL_STEP = 64;
  static constexpr uint8_t ORIGIN_NONE = 4;
  // Level, doses left, target | source << 2.
  static constexpr size_t PIXEL_BYTES = 3;

  static constexpr size_t pixelCount(const uint16_t width, const uint16_t height) {
    return static_cast<size_t>(width) * height;
  }
  static constexpr size_t bytesFor(const uint16_t width, const uint16_t height) {
    return pixelCount(width, height) * PIXEL_BYTES;
  }
  static constexpr size_t historyBytesFor(const uint16_t width, const uint16_t height) {
    return pixelCount(width, height) * 3;
  }
  static constexpr size_t weakBytesFor(const uint16_t width, const uint16_t height) {
    return (pixelCount(width, height) + 7) / 8;
  }

  GrayModel() = default;
  // `pixels` holds bytesFor(width, height) bytes, `history` historyBytesFor or is null, `weak` weakBytesFor or is
  // null; all outlive the view. Call reset() before use.
  GrayModel(uint8_t* pixels, uint16_t width, uint16_t height, uint8_t* history = nullptr, uint8_t* weak = nullptr);

  bool valid() const { return pixels_ != nullptr && width_ > 0 && height_ > 0; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  bool hasHistory() const { return history_ != nullptr; }
  bool hasWeak() const { return weak_ != nullptr; }

  // Every pixel settled on `shade` (what a cleaning refresh to that shade leaves): level shade * 64, no doses left,
  // target and source `shade`; no history origin; no weak pixel.
  void reset(Shade shade);

  // The dose from `level` to `target`: dose[level / 64][target] and the next shade's dose interpolated by level % 64,
  // rounded up; 0 only when level is target * 64.
  static uint8_t baseDose(uint8_t level, uint8_t target, const GrayPreset& preset);

  // Moves pixel (x, y) to `target` from its current level; nothing when its target is already `target` or it is
  // outside the model. Budget: the base dose, or for a pixel interrupted mid-transition the base scaled by the
  // retarget percent toward the target's side of its level (retargetDarkPercent when `target` lies below the level,
  // retargetLightPercent when above), rounded up, DOSE_MIN..DOSE_MAX. With history, a route back to the pixel's origin
  // adjusts it (a completed return by the route's delta, a reversal by its percent), and the origin and step counters
  // restart; with a weak bitmap, the pixel's weak eligibility is set (W > L from settled white) or cleared.
  void retarget(int x, int y, Shade target, const GrayPreset& preset);
  // retarget() for every pixel of [x, x + w) x [y, y + h), clipped to the model.
  void retargetRect(int x, int y, int w, int h, Shade target, const GrayPreset& preset);

  // Clears both mask pairs and sets each pixel with doses left: in `drive`, white when it lightens (target * 64 >
  // level), else black (both, the weak row of a backend that has one, when `weakRow` and the pixel is weak-eligible);
  // in `follow` the same when its hold bit is off and it has more than one dose left. Both pairs must match the model's
  // size. Returns the pixels driven; 0 for a mismatched pair (nothing set).
  size_t buildMasks(MaskPlanes& drive, MaskPlanes& follow, const GrayPreset& preset, bool weakRow) const;
  // Commits the slot buildMasks planned: nothing unless `driveDone`; one dose per driven pixel, and a second for a
  // follow-up pixel when `followDone`. History step counters count each dose (saturating at 255).
  void commit(bool driveDone, bool followDone, const GrayPreset& preset);

  // Pixel (x, y)'s state; 0 (ORIGIN_NONE for origin, false for weak) outside the model or without the array.
  uint8_t level(int x, int y) const;
  uint8_t remaining(int x, int y) const;
  uint8_t target(int x, int y) const;
  uint8_t source(int x, int y) const;
  uint8_t origin(int x, int y) const;
  uint8_t blackSteps(int x, int y) const;
  uint8_t whiteSteps(int x, int y) const;
  bool weakEligible(int x, int y) const;
  // Pixels with doses left.
  size_t activeCount() const;

 private:
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
  size_t indexOf(int x, int y) const { return static_cast<size_t>(y) * width_ + static_cast<size_t>(x); }
  uint8_t* pixelAt(size_t index) const { return pixels_ + PIXEL_BYTES * index; }
  bool weakBit(size_t index) const;
  void setWeakBit(size_t index, bool on);
  // Whether the follow-up scan drives pixel `pixel` (its PIXEL_BYTES) again (hold bit off, more than one dose left).
  static bool followsUp(const uint8_t* pixel, const GrayPreset& preset);
  // One dose for pixel `index`, counted into its history toward `lighten`.
  void deliverDose(size_t index, bool lighten);

  uint8_t* pixels_ = nullptr;
  uint8_t* history_ = nullptr;
  uint8_t* weak_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
};

}  // namespace GameDisplayLab

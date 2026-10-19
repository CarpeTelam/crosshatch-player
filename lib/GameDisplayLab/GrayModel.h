#pragma once

#include <cstddef>
#include <cstdint>

#include "GrayPreset.h"
#include "MaskPlanes.h"

// The gray engine's per-pixel model (the display-lab epic, R3): InkBoy's design (the D2 round 2 digest, Q2-Q5) over
// the lab's full-screen masks. Pure C++, pinned by test/game_display_lab/test_gray_model.cpp.
//
// A non-owning view over caller buffers in panel coordinates (the masks' pixel order), each pixel PIXEL_BYTES bytes:
// its modelled level (0..192, level * 32 over the seven levels of story dlab-g2; 0 black, 192 white), the doses still
// to deliver (0..DOSE_MAX, the byte's low five bits) with a pending second leg's target in its high three (0 none, else
// level + 1: a transition with a via, GrayPreset::via), then target | source << 3 (the level it moves to; the level
// nearest its level at the last retarget, which picks the hold bit).
// Optional: a history array of three bytes per pixel (origin level, or ORIGIN_NONE; the doses delivered darker and
// lighter since the last retarget), and a weak bitmap (one bit per pixel, row-major, most significant bit first: a W >
// L pixel that darkens through the weak row). Without them (null) history routes and weak mode are off.
//
// One slot (GraySlot.h): buildMasks puts every pixel with doses left in the drive mask toward its target, and in the
// follow-up mask too when its transition's hold bit is off and it has two or more doses left; commit then advances
// each driven pixel one dose (two for follow-up pixels when the follow-up scan was delivered too): level +=
// (target * 32 - level) / remaining, remaining - 1, so the last dose lands on the target exactly, and a pixel whose
// first leg has landed starts its second. A scan that was not delivered commits nothing.
namespace GameDisplayLab {

class GrayModel {
 public:
  static constexpr uint8_t LEVEL_STEP = 32;
  static constexpr uint8_t ORIGIN_NONE = 7;
  // Level, doses left | second leg's target, target | source << 3.
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

  // Every pixel settled on `shade` (what a cleaning refresh to that shade leaves): level shade * 32, no doses left, no
  // second leg, target and source `shade`; no history origin; no weak pixel. Nothing for TRANSPARENT.
  void reset(Shade shade);

  // The dose from `level` to `target`: dose[level / 32][target] and the next level's dose interpolated by level % 32,
  // rounded up; 0 when level is target * 32 or `target` is past white (TRANSPARENT).
  static uint8_t baseDose(uint8_t level, uint8_t target, const GrayPreset& preset);

  // Moves pixel (x, y) to `target` from its current level; nothing when `target` is already where it is going (its
  // second leg's target when it has one), when `target` is past white (TRANSPARENT), or when it is outside the model.
  // A transition with a via (preset.via[from][target], `from` the level nearest the pixel's level, which is not the via
  // level itself) runs as two legs: this retarget moves the pixel to the via and keeps `target` as its second leg,
  // which commit starts, from the settled via level, once the first has landed (story dlab-g2, D14). A retarget to the
  // via level of a pixel on its first leg drops the second. Each leg's budget: the base dose, or for a pixel
  // interrupted mid-transition the base scaled by the retarget percent toward the leg target's side of its level
  // (retargetDarkPercent when it lies below the level, retargetLightPercent when above), rounded up,
  // DOSE_MIN..DOSE_MAX. With history, a return to the pixel's origin adjusts it by the route of the direction it
  // returns in (a completed return by the route's delta, a reversal by its percent), and the origin (the level of a
  // settled pixel) and step counters restart; with a weak bitmap, the pixel's weak eligibility is set (W > L from
  // settled white) or cleared.
  void retarget(int x, int y, Shade target, const GrayPreset& preset);
  // retarget() for every pixel of [x, x + w) x [y, y + h), clipped to the model.
  void retargetRect(int x, int y, int w, int h, Shade target, const GrayPreset& preset);
  // A level-coded bitmap (story dlab-g2, the owner's ask for sprites): `levels` holds w * h bytes, row-major, one per
  // pixel, 0..6 a level and 7 (Shade::TRANSPARENT) transparent; bitmap pixel (i, j) is model pixel (x + i, y + j),
  // clipped to the model. Every pixel at a level is retargeted to it; a transparent one keeps its level, doses and
  // target. A value above 7 is treated as transparent and counted: returns how many of the model's pixels had one
  // (clipped ones are neither retargeted nor counted; the caller logs them once; this model is std-only). Nothing, 0,
  // for a null bitmap or an empty rectangle.
  size_t retargetBitmap(int x, int y, int w, int h, const uint8_t* levels, const GrayPreset& preset);
  // The ladder's pulses (story dlab-g1): every pixel of [x, x + w) x [y, y + h), clipped to the model, gets exactly
  // `doses` doses (at most DOSE_MAX) toward black (Toward::Dark: target K) or white (Toward::Light: target W), set
  // directly, without the dose table, the retarget percents or the history routes; even a pixel already moving to
  // that target, or already on it (each dose then leaves its level where it is), takes the new count. The bookkeeping
  // is a retarget's, so buildMasks, commit and settle detection work unchanged: the level is kept, the source is the
  // shade nearest the level (which picks the hold bit); the history origin is cleared with its step counters (a driven
  // pixel has no route), the weak bit (a pulse is the plain one-frame drive) and any second leg. `doses` 0 changes
  // nothing.
  void driveRect(int x, int y, int w, int h, Toward toward, uint8_t doses);

  // Clears both mask pairs and sets each pixel with doses left: in `drive`, white when it lightens (a W target, or a
  // middle target above its level; a pixel driveRect drives toward the end it is already on keeps that direction),
  // else black (both, the weak row of a backend that has one, when `weakRow` and the pixel is weak-eligible); in
  // `follow` the same when its hold bit is off and it has more than one dose left. Both pairs must match the model's
  // size. Returns the pixels driven; 0 for a mismatched pair (nothing set).
  size_t buildMasks(MaskPlanes& drive, MaskPlanes& follow, const GrayPreset& preset, bool weakRow) const;
  // Commits the slot buildMasks planned: nothing unless `driveDone`; one dose per driven pixel, and a second for a
  // follow-up pixel when `followDone`. History step counters count each dose (saturating at 255).
  void commit(bool driveDone, bool followDone, const GrayPreset& preset);

  // Pixel (x, y)'s state; 0 (ORIGIN_NONE for origin and the second leg, false for weak) outside the model or without
  // the array. target() is the current leg's; nextTarget() the second leg's, or ORIGIN_NONE without one.
  uint8_t level(int x, int y) const;
  uint8_t remaining(int x, int y) const;
  uint8_t target(int x, int y) const;
  uint8_t nextTarget(int x, int y) const;
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
  // Whether the follow-up scan drives pixel `pixel` (its PIXEL_BYTES) again (hold bit off, more than one dose left);
  // `holdRows` is holdRowsOf(preset), derived once per slot.
  static bool followsUp(const uint8_t* pixel, const uint8_t* holdRows);
  // One dose for pixel `index`, counted into its history toward `lighten`; the second leg starts when the first lands.
  void deliverDose(size_t index, bool lighten, const GrayPreset& preset);
  // retarget()'s bookkeeping for one leg of pixel `index` toward level `t` (below SHADE_COUNT, not its current target):
  // the budget, the history routes, the weak bit, and the pixel's bytes with `next` as its second leg (ORIGIN_NONE:
  // none). A `secondLeg` (a via's, started by commit) leaves the history alone: the origin and the step counters are
  // the first leg's, so a later return is judged against where the transition started, not the via level.
  void retargetLeg(size_t index, uint8_t t, uint8_t next, const GrayPreset& preset, bool secondLeg = false);

  uint8_t* pixels_ = nullptr;
  uint8_t* history_ = nullptr;
  uint8_t* weak_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
};

}  // namespace GameDisplayLab

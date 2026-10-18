#include "GrayModel.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// A pixel's bytes: its level, its doses left, and its shades (target | source << SOURCE_SHIFT).
constexpr size_t LEVEL_BYTE = 0;
constexpr size_t REMAINING_BYTE = 1;
constexpr size_t SHADES_BYTE = 2;
static_assert(SHADES_BYTE < GrayModel::PIXEL_BYTES, "a pixel holds its three bytes");
constexpr uint8_t SOURCE_SHIFT = 2;
constexpr uint8_t SHADE_MASK = 0x03;
constexpr uint8_t STEPS_MAX = 255;
constexpr uint8_t TOP_SHADE = SHADE_COUNT - 1;
constexpr uint8_t TOP_LEVEL = TOP_SHADE * GrayModel::LEVEL_STEP;  // 192, white
// History bytes per pixel: origin, black steps, white steps.
constexpr size_t HISTORY_ORIGIN = 0;
constexpr size_t HISTORY_BLACK = 1;
constexpr size_t HISTORY_WHITE = 2;

constexpr uint8_t targetOf(const uint8_t shades) { return shades & SHADE_MASK; }
constexpr uint8_t sourceOf(const uint8_t shades) { return (shades >> SOURCE_SHIFT) & SHADE_MASK; }
constexpr uint8_t packShades(const uint8_t target, const uint8_t source) {
  return static_cast<uint8_t>((target & SHADE_MASK) | ((source & SHADE_MASK) << SOURCE_SHIFT));
}

// Writes a pixel's three bytes.
void writePixel(uint8_t* pixel, const uint8_t level, const uint8_t remaining, const uint8_t target,
                const uint8_t source) {
  pixel[LEVEL_BYTE] = level;
  pixel[REMAINING_BYTE] = remaining;
  pixel[SHADES_BYTE] = packShades(target, source);
}

uint8_t clampDose(const int value) {
  if (value < DOSE_MIN) return DOSE_MIN;
  if (value > DOSE_MAX) return DOSE_MAX;
  return static_cast<uint8_t>(value);
}

// `base` scaled by `percent`, rounded up, clamped DOSE_MIN..DOSE_MAX (the digest does not show InkBoy's `scaled`
// table; at 100 % this is the identity).
uint8_t scaledDose(const uint8_t base, const unsigned percent) {
  return clampDose(static_cast<int>((base * percent + 99u) / 100u));
}

// The history route of a pixel whose origin is `origin` and whose abandoned target is `via`, or -1.
int routeOf(const uint8_t origin, const uint8_t via) {
  constexpr uint8_t K = static_cast<uint8_t>(Shade::K);
  constexpr uint8_t D = static_cast<uint8_t>(Shade::D);
  constexpr uint8_t L = static_cast<uint8_t>(Shade::L);
  constexpr uint8_t W = static_cast<uint8_t>(Shade::W);
  if (origin == L && via == K) return static_cast<int>(Route::LKL);
  if (origin == D && via == K) return static_cast<int>(Route::DKD);
  if (origin == D && via == W) return static_cast<int>(Route::DWD);
  if (origin == L && via == W) return static_cast<int>(Route::LWL);
  if (origin == D && via == L) return static_cast<int>(Route::DLD);
  if (origin == L && via == D) return static_cast<int>(Route::LDL);
  return -1;
}

// Whether a pixel with doses left is driven toward white: always toward a W target and never toward a K one, whatever
// its level (a driveRect pulse on a pixel already at that end still goes that way: story dlab-g1), and toward a middle
// target when it lies above the level (retarget never leaves doses on a pixel already on its target).
bool lightens(const uint8_t* pixel) {
  const uint8_t target = targetOf(pixel[SHADES_BYTE]);
  if (target == TOP_SHADE) return true;
  if (target == static_cast<uint8_t>(Shade::K)) return false;
  return target * GrayModel::LEVEL_STEP > pixel[LEVEL_BYTE];
}

// The shade nearest `level` (a level past white is white).
uint8_t nearestShadeOf(const uint8_t level) {
  const unsigned nearest = (level + GrayModel::LEVEL_STEP / 2u) / GrayModel::LEVEL_STEP;
  return static_cast<uint8_t>(nearest > TOP_SHADE ? TOP_SHADE : nearest);
}

// Whether the D>L>D or L>D>L route changes a budget, which makes a pixel record its origin on any new target.
bool midRoutesActive(const GrayPreset& preset) {
  const auto active = [&preset](const Route route) {
    const size_t r = static_cast<size_t>(route);
    return preset.returnDelta[r] != 0 || preset.reversePercent[r] != 0;
  };
  return active(Route::DLD) || active(Route::LDL);
}

}  // namespace

GrayModel::GrayModel(uint8_t* pixels, const uint16_t width, const uint16_t height, uint8_t* history, uint8_t* weak)
    : pixels_(pixels), history_(history), weak_(weak), width_(width), height_(height) {}

void GrayModel::reset(const Shade shade) {
  if (!valid()) return;
  const uint8_t s = static_cast<uint8_t>(shade);
  const uint8_t settledLevel = static_cast<uint8_t>(s * LEVEL_STEP);
  const size_t count = pixelCount(width_, height_);
  for (size_t i = 0; i < count; ++i) writePixel(pixelAt(i), settledLevel, 0, s, s);
  if (history_) {
    for (size_t i = 0; i < count; ++i) {
      history_[3 * i + HISTORY_ORIGIN] = ORIGIN_NONE;
      history_[3 * i + HISTORY_BLACK] = 0;
      history_[3 * i + HISTORY_WHITE] = 0;
    }
  }
  if (weak_) memset(weak_, 0, weakBytesFor(width_, height_));
}

uint8_t GrayModel::baseDose(const uint8_t level, const uint8_t target, const GrayPreset& preset) {
  const unsigned clamped = level > TOP_LEVEL ? TOP_LEVEL : level;
  const unsigned low = clamped / LEVEL_STEP;
  const unsigned fraction = clamped % LEVEL_STEP;
  const unsigned high = low < TOP_SHADE ? low + 1 : low;
  const unsigned t = target & SHADE_MASK;
  return static_cast<uint8_t>(
      (preset.dose[low][t] * (LEVEL_STEP - fraction) + preset.dose[high][t] * fraction + LEVEL_STEP - 1) / LEVEL_STEP);
}

void GrayModel::retarget(const int x, const int y, const Shade target, const GrayPreset& preset) {
  if (!valid() || !inside(x, y)) return;
  const size_t i = indexOf(x, y);
  uint8_t* pixel = pixelAt(i);
  const uint8_t current = pixel[LEVEL_BYTE];
  const uint8_t oldTarget = targetOf(pixel[SHADES_BYTE]);
  const uint8_t oldRemaining = pixel[REMAINING_BYTE];
  const uint8_t t = static_cast<uint8_t>(target);
  if (t == oldTarget) return;

  const uint8_t base = baseDose(current, t, preset);
  // A pixel interrupted mid-transition: the retarget percent of the side its new target lies on (D12: one percent
  // overshot toward white while still falling short toward black). base != 0 means the target is not its level.
  const Toward toward = t * LEVEL_STEP < current ? Toward::Dark : Toward::Light;
  uint8_t budget = oldRemaining != 0 && base != 0 ? scaledDose(base, preset.retargetPercent(toward)) : base;
  const bool settled = oldRemaining == 0 && current % LEVEL_STEP == 0;

  if (history_ && preset.historyMode != 0) {
    uint8_t* h = history_ + 3 * i;
    const uint8_t originShade = h[HISTORY_ORIGIN];
    const int route = originShade == t ? routeOf(originShade, oldTarget) : -1;
    if (route >= 0) {
      const uint8_t via = oldTarget;
      const uint8_t steps = via < originShade ? h[HISTORY_BLACK] : h[HISTORY_WHITE];
      // A completed return: the pixel went origin > via cleanly (exactly the table's dose) and settled there.
      if (oldRemaining == 0 && (preset.historyMode & HISTORY_RETURN) != 0 && current == via * LEVEL_STEP &&
          steps == preset.dose[originShade][via]) {
        const int delta = preset.returnDelta[route];
        if (delta != 0) budget = clampDose(base + delta);
      } else if (oldRemaining != 0 && base != 0 && (preset.historyMode & HISTORY_REVERSE) != 0) {
        // A reversal: the pixel turns back to its origin before reaching via.
        const uint8_t percent = preset.reversePercent[route];
        if (percent != 0) budget = scaledDose(base, percent);
      }
    }
    const bool recordOrigin = t == static_cast<uint8_t>(Shade::K) || t == TOP_SHADE || midRoutesActive(preset);
    h[HISTORY_ORIGIN] = recordOrigin && settled ? static_cast<uint8_t>(current / LEVEL_STEP) : ORIGIN_NONE;
    h[HISTORY_BLACK] = 0;
    h[HISTORY_WHITE] = 0;
  }

  if (weak_) {
    // Eligibility is frozen here: a W > L pixel settled on white. Any other retarget clears it.
    setWeakBit(
        i, t == static_cast<uint8_t>(Shade::L) && oldTarget == TOP_SHADE && oldRemaining == 0 && current == TOP_LEVEL);
  }

  const unsigned nearest = (current + LEVEL_STEP / 2u) / LEVEL_STEP;
  const uint8_t nearestShade = static_cast<uint8_t>(nearest > TOP_SHADE ? TOP_SHADE : nearest);
  writePixel(pixel, current, budget, t, nearestShade);
}

void GrayModel::retargetRect(const int x, const int y, const int w, const int h, const Shade target,
                             const GrayPreset& preset) {
  if (!valid() || w <= 0 || h <= 0) return;
  const int x0 = x < 0 ? 0 : x;
  const int y0 = y < 0 ? 0 : y;
  const int x1 = x + w > width_ ? width_ : x + w;
  const int y1 = y + h > height_ ? height_ : y + h;
  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) retarget(px, py, target, preset);
  }
}

void GrayModel::driveRect(const int x, const int y, const int w, const int h, const Toward toward,
                          const uint8_t doses) {
  if (!valid() || w <= 0 || h <= 0 || doses == 0) return;
  const uint8_t count = doses > DOSE_MAX ? DOSE_MAX : doses;
  const uint8_t target = toward == Toward::Dark ? static_cast<uint8_t>(Shade::K) : TOP_SHADE;
  const int x0 = x < 0 ? 0 : x;
  const int y0 = y < 0 ? 0 : y;
  const int x1 = x + w > width_ ? width_ : x + w;
  const int y1 = y + h > height_ ? height_ : y + h;
  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) {
      const size_t i = indexOf(px, py);
      uint8_t* pixel = pixelAt(i);
      const uint8_t current = pixel[LEVEL_BYTE];
      if (history_) {
        history_[3 * i + HISTORY_ORIGIN] = ORIGIN_NONE;
        history_[3 * i + HISTORY_BLACK] = 0;
        history_[3 * i + HISTORY_WHITE] = 0;
      }
      setWeakBit(i, false);
      writePixel(pixel, current, count, target, nearestShadeOf(current));
    }
  }
}

bool GrayModel::followsUp(const uint8_t* pixel, const GrayPreset& preset) {
  const uint8_t shades = pixel[SHADES_BYTE];
  return pixel[REMAINING_BYTE] > 1 && (preset.holdMask & holdBit(sourceOf(shades), targetOf(shades))) == 0;
}

size_t GrayModel::buildMasks(MaskPlanes& drive, MaskPlanes& follow, const GrayPreset& preset,
                             const bool weakRow) const {
  if (!valid()) return 0;
  const bool sized = drive.valid() && follow.valid() && drive.width() == width_ && drive.height() == height_ &&
                     follow.width() == width_ && follow.height() == height_;
  if (!sized) return 0;
  drive.clear();
  follow.clear();
  size_t driven = 0;
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const size_t i = indexOf(x, y);
      const uint8_t* pixel = pixelAt(i);
      if (pixel[REMAINING_BYTE] == 0) continue;
      const bool lighten = lightens(pixel);
      const bool weak = weakRow && !lighten && weakBit(i);
      const bool again = followsUp(pixel, preset);
      // A weak pixel is set in both masks: the third (weak) row of a backend whose supportsWeakRow() is true. D1 itself
      // leaves a both-masks pixel undefined (the SSD1677 holds it), so only `weakRow` sets one.
      if (weak || lighten) {
        drive.setPixel(MaskPlanes::Mask::White, x, y);
        if (again) follow.setPixel(MaskPlanes::Mask::White, x, y);
      }
      if (weak || !lighten) {
        drive.setPixel(MaskPlanes::Mask::Black, x, y);
        if (again) follow.setPixel(MaskPlanes::Mask::Black, x, y);
      }
      ++driven;
    }
  }
  return driven;
}

void GrayModel::deliverDose(const size_t index, const bool lighten) {
  uint8_t* pixel = pixelAt(index);
  const uint8_t left = pixel[REMAINING_BYTE];
  if (left == 0) return;
  const int current = pixel[LEVEL_BYTE];
  const int goal = targetOf(pixel[SHADES_BYTE]) * LEVEL_STEP;
  // C++ division truncates toward zero, so the level never passes the goal and the last dose lands on it.
  pixel[LEVEL_BYTE] = static_cast<uint8_t>(current + (goal - current) / left);
  pixel[REMAINING_BYTE] = static_cast<uint8_t>(left - 1);
  if (history_) {
    uint8_t& steps = history_[3 * index + (lighten ? HISTORY_WHITE : HISTORY_BLACK)];
    if (steps < STEPS_MAX) ++steps;
  }
}

void GrayModel::commit(const bool driveDone, const bool followDone, const GrayPreset& preset) {
  if (!valid() || !driveDone) return;
  const size_t count = pixelCount(width_, height_);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* pixel = pixelAt(i);
    if (pixel[REMAINING_BYTE] == 0) continue;
    // The direction and the follow-up decision are buildMasks', read from the state it read.
    const bool lighten = lightens(pixel);
    const bool again = followDone && followsUp(pixel, preset);
    deliverDose(i, lighten);
    if (again) deliverDose(i, lighten);
  }
}

bool GrayModel::weakBit(const size_t index) const {
  return weak_ != nullptr && (weak_[index / 8] & (0x80u >> (index % 8))) != 0;
}

void GrayModel::setWeakBit(const size_t index, const bool on) {
  if (weak_ == nullptr) return;
  const uint8_t bit = static_cast<uint8_t>(0x80u >> (index % 8));
  weak_[index / 8] = static_cast<uint8_t>(on ? weak_[index / 8] | bit : weak_[index / 8] & ~bit);
}

uint8_t GrayModel::level(const int x, const int y) const {
  return valid() && inside(x, y) ? pixelAt(indexOf(x, y))[LEVEL_BYTE] : 0;
}

uint8_t GrayModel::remaining(const int x, const int y) const {
  return valid() && inside(x, y) ? pixelAt(indexOf(x, y))[REMAINING_BYTE] : 0;
}

uint8_t GrayModel::target(const int x, const int y) const {
  return valid() && inside(x, y) ? targetOf(pixelAt(indexOf(x, y))[SHADES_BYTE]) : 0;
}

uint8_t GrayModel::source(const int x, const int y) const {
  return valid() && inside(x, y) ? sourceOf(pixelAt(indexOf(x, y))[SHADES_BYTE]) : 0;
}

uint8_t GrayModel::origin(const int x, const int y) const {
  return history_ && inside(x, y) ? history_[3 * indexOf(x, y) + HISTORY_ORIGIN] : ORIGIN_NONE;
}

uint8_t GrayModel::blackSteps(const int x, const int y) const {
  return history_ && inside(x, y) ? history_[3 * indexOf(x, y) + HISTORY_BLACK] : 0;
}

uint8_t GrayModel::whiteSteps(const int x, const int y) const {
  return history_ && inside(x, y) ? history_[3 * indexOf(x, y) + HISTORY_WHITE] : 0;
}

bool GrayModel::weakEligible(const int x, const int y) const { return inside(x, y) && weakBit(indexOf(x, y)); }

size_t GrayModel::activeCount() const {
  if (!valid()) return 0;
  size_t active = 0;
  const size_t count = pixelCount(width_, height_);
  for (size_t i = 0; i < count; ++i) {
    if (pixelAt(i)[REMAINING_BYTE] != 0) ++active;
  }
  return active;
}

}  // namespace GameDisplayLab

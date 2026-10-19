#include "GrayModel.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// A pixel's bytes: its level, its doses left (with a second leg's target), and its levels (target | source <<
// SOURCE_SHIFT).
constexpr size_t LEVEL_BYTE = 0;
constexpr size_t REMAINING_BYTE = 1;
constexpr size_t SHADES_BYTE = 2;
static_assert(SHADES_BYTE < GrayModel::PIXEL_BYTES, "a pixel holds its three bytes");
constexpr uint8_t SOURCE_SHIFT = 3;
constexpr uint8_t SHADE_MASK = 0x07;
static_assert(SHADE_COUNT <= SHADE_MASK + 1, "a level fits three bits");
// The doses-left byte: the count in its low five bits, a second leg's target + 1 in its high three (0: none).
constexpr uint8_t DOSES_MASK = 0x1F;
constexpr uint8_t NEXT_SHIFT = 5;
static_assert(DOSE_MAX <= DOSES_MASK, "the dose count fits five bits");
static_assert(SHADE_COUNT < (1u << (8 - NEXT_SHIFT)), "a second leg's target + 1 fits three bits");
constexpr uint8_t STEPS_MAX = 255;
constexpr uint8_t TOP_SHADE = SHADE_COUNT - 1;
constexpr uint8_t TOP_LEVEL = TOP_SHADE * GrayModel::LEVEL_STEP;  // 192, white
static_assert(GrayModel::ORIGIN_NONE >= SHADE_COUNT, "no origin is no level");
// History bytes per pixel: origin, black steps, white steps.
constexpr size_t HISTORY_ORIGIN = 0;
constexpr size_t HISTORY_BLACK = 1;
constexpr size_t HISTORY_WHITE = 2;

constexpr uint8_t targetOf(const uint8_t shades) { return shades & SHADE_MASK; }
constexpr uint8_t sourceOf(const uint8_t shades) { return (shades >> SOURCE_SHIFT) & SHADE_MASK; }
constexpr uint8_t packShades(const uint8_t target, const uint8_t source) {
  return static_cast<uint8_t>((target & SHADE_MASK) | ((source & SHADE_MASK) << SOURCE_SHIFT));
}
constexpr uint8_t dosesOf(const uint8_t* pixel) { return pixel[REMAINING_BYTE] & DOSES_MASK; }
// The second leg's target, or ORIGIN_NONE.
constexpr uint8_t nextOf(const uint8_t* pixel) {
  const uint8_t code = static_cast<uint8_t>(pixel[REMAINING_BYTE] >> NEXT_SHIFT);
  return code == 0 ? GrayModel::ORIGIN_NONE : static_cast<uint8_t>(code - 1);
}

// Writes a pixel's three bytes; `next` is the second leg's target or ORIGIN_NONE.
void writePixel(uint8_t* pixel, const uint8_t level, const uint8_t remaining, const uint8_t target,
                const uint8_t source, const uint8_t next = GrayModel::ORIGIN_NONE) {
  const uint8_t code = next < SHADE_COUNT ? static_cast<uint8_t>(next + 1) : 0;
  pixel[LEVEL_BYTE] = level;
  pixel[REMAINING_BYTE] = static_cast<uint8_t>((remaining & DOSES_MASK) | (code << NEXT_SHIFT));
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

// Whether a pixel with doses left is driven toward white: always toward a white target and never toward a black one,
// whatever its level (a driveRect pulse on a pixel already at that end still goes that way: story dlab-g1), and toward
// a middle target when it lies above the level (retarget never leaves doses on a pixel already on its target).
bool lightens(const uint8_t* pixel) {
  const uint8_t target = targetOf(pixel[SHADES_BYTE]);
  if (target == TOP_SHADE) return true;
  if (target == static_cast<uint8_t>(Shade::K)) return false;
  return target * GrayModel::LEVEL_STEP > pixel[LEVEL_BYTE];
}

// The level nearest `level` (a level past white is white).
uint8_t nearestShadeOf(const uint8_t level) {
  const unsigned nearest = (level + GrayModel::LEVEL_STEP / 2u) / GrayModel::LEVEL_STEP;
  return static_cast<uint8_t>(nearest > TOP_SHADE ? TOP_SHADE : nearest);
}

}  // namespace

GrayModel::GrayModel(uint8_t* pixels, const uint16_t width, const uint16_t height, uint8_t* history, uint8_t* weak)
    : pixels_(pixels), history_(history), weak_(weak), width_(width), height_(height) {}

void GrayModel::reset(const Shade shade) {
  const uint8_t s = static_cast<uint8_t>(shade);
  if (!valid() || s >= SHADE_COUNT) return;
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
  if (target >= SHADE_COUNT) return 0;  // TRANSPARENT: no level to move to
  const unsigned t = target;
  return static_cast<uint8_t>(
      (preset.dose[low][t] * (LEVEL_STEP - fraction) + preset.dose[high][t] * fraction + LEVEL_STEP - 1) / LEVEL_STEP);
}

void GrayModel::retarget(const int x, const int y, const Shade target, const GrayPreset& preset) {
  const uint8_t t = static_cast<uint8_t>(target);
  if (!valid() || !inside(x, y) || t >= SHADE_COUNT) return;
  const size_t i = indexOf(x, y);
  uint8_t* pixel = pixelAt(i);
  const uint8_t legTarget = targetOf(pixel[SHADES_BYTE]);
  const uint8_t next = nextOf(pixel);
  // Already going there: its second leg's target, or (without one) its target.
  if (t == (next < SHADE_COUNT ? next : legTarget)) return;
  if (t == legTarget) {
    // On its first leg toward the via level, now the target itself: the second leg is dropped, the doses kept.
    writePixel(pixel, pixel[LEVEL_BYTE], dosesOf(pixel), legTarget, sourceOf(pixel[SHADES_BYTE]));
    return;
  }
  // A via (D14: one pulse from black already passes level 2, so 0>1 goes to 2 first): from the level nearest the
  // pixel's, unless the pixel is on the via level already (then the transition is a plain one).
  const uint8_t from = nearestShadeOf(pixel[LEVEL_BYTE]);
  const uint8_t via = preset.via[from][t];
  const bool twoLegs = via < SHADE_COUNT && via != t && pixel[LEVEL_BYTE] != via * LEVEL_STEP;
  if (twoLegs) {
    retargetLeg(i, via, t, preset);
  } else {
    retargetLeg(i, t, ORIGIN_NONE, preset);
  }
}

void GrayModel::retargetLeg(const size_t i, const uint8_t t, const uint8_t next, const GrayPreset& preset) {
  uint8_t* pixel = pixelAt(i);
  const uint8_t current = pixel[LEVEL_BYTE];
  const uint8_t oldTarget = targetOf(pixel[SHADES_BYTE]);
  const uint8_t oldRemaining = dosesOf(pixel);
  // A leg toward the level the pixel already moves to (a via leg it is on): keep its doses, take the second leg.
  if (t == oldTarget && oldRemaining != 0) {
    writePixel(pixel, current, oldRemaining, t, sourceOf(pixel[SHADES_BYTE]), next);
    return;
  }

  const uint8_t base = baseDose(current, t, preset);
  // A pixel interrupted mid-transition: the retarget percent of the side its new target lies on (D12: one percent
  // overshot toward white while still falling short toward black). base != 0 means the target is not its level.
  const Toward toward = t * LEVEL_STEP < current ? Toward::Dark : Toward::Light;
  uint8_t budget = oldRemaining != 0 && base != 0 ? scaledDose(base, preset.retargetPercent(toward)) : base;
  const bool settled = oldRemaining == 0 && current % LEVEL_STEP == 0;

  if (history_ && preset.historyMode != 0) {
    uint8_t* h = history_ + 3 * i;
    const uint8_t originShade = h[HISTORY_ORIGIN];
    // A return to the origin: the route of the direction it returns in (story dlab-g2: two directional routes).
    if (originShade == t && oldTarget != originShade && base != 0) {
      const size_t route = routeIndex(toward);
      const uint8_t via = oldTarget;
      const uint8_t steps = via < originShade ? h[HISTORY_BLACK] : h[HISTORY_WHITE];
      // A completed return: the pixel went origin > via cleanly (exactly the table's dose) and settled there.
      if (oldRemaining == 0 && (preset.historyMode & HISTORY_RETURN) != 0 && current == via * LEVEL_STEP &&
          steps == preset.dose[originShade][via]) {
        const int delta = preset.returnDelta[route];
        if (delta != 0) budget = clampDose(base + delta);
      } else if (oldRemaining != 0 && (preset.historyMode & HISTORY_REVERSE) != 0) {
        // A reversal: the pixel turns back to its origin before reaching via.
        const uint8_t percent = preset.reversePercent[route];
        if (percent != 0) budget = scaledDose(base, percent);
      }
    }
    // Every settled pixel records where it starts from, so any return to it takes its direction's route.
    h[HISTORY_ORIGIN] = settled ? static_cast<uint8_t>(current / LEVEL_STEP) : ORIGIN_NONE;
    h[HISTORY_BLACK] = 0;
    h[HISTORY_WHITE] = 0;
  }

  if (weak_) {
    // Eligibility is frozen here: a W > L pixel settled on white. Any other retarget clears it.
    setWeakBit(
        i, t == static_cast<uint8_t>(Shade::L) && oldTarget == TOP_SHADE && oldRemaining == 0 && current == TOP_LEVEL);
  }

  writePixel(pixel, current, budget, t, nearestShadeOf(current), next);
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

size_t GrayModel::retargetBitmap(const int x, const int y, const int w, const int h, const uint8_t* levels,
                                 const GrayPreset& preset) {
  if (!valid() || levels == nullptr || w <= 0 || h <= 0) return 0;
  constexpr uint8_t TRANSPARENT = static_cast<uint8_t>(Shade::TRANSPARENT);
  size_t clamped = 0;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      const uint8_t value = levels[static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)];
      if (value > TRANSPARENT) ++clamped;  // treated as transparent: the caller logs it
      if (value >= TRANSPARENT) continue;  // a transparent pixel keeps its level, doses and target
      retarget(x + i, y + j, static_cast<Shade>(value), preset);
    }
  }
  return clamped;
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
  return dosesOf(pixel) > 1 && (preset.holdMask & holdBit(sourceOf(shades), targetOf(shades))) == 0;
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
      if (dosesOf(pixel) == 0) continue;
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

void GrayModel::deliverDose(const size_t index, const bool lighten, const GrayPreset& preset) {
  uint8_t* pixel = pixelAt(index);
  const uint8_t left = dosesOf(pixel);
  if (left == 0) return;
  const int current = pixel[LEVEL_BYTE];
  const int goal = targetOf(pixel[SHADES_BYTE]) * LEVEL_STEP;
  const uint8_t next = nextOf(pixel);
  // C++ division truncates toward zero, so the level never passes the goal and the last dose lands on it.
  pixel[LEVEL_BYTE] = static_cast<uint8_t>(current + (goal - current) / left);
  pixel[REMAINING_BYTE] = static_cast<uint8_t>((pixel[REMAINING_BYTE] & ~DOSES_MASK) | (left - 1));
  if (history_) {
    uint8_t& steps = history_[3 * index + (lighten ? HISTORY_WHITE : HISTORY_BLACK)];
    if (steps < STEPS_MAX) ++steps;
  }
  // The first leg has landed on the via level: the second starts from it, settled, through the retarget bookkeeping
  // (its own dose, no via of its own). A follow-up dose never reaches here with one dose left (followsUp needs two).
  if (left == 1 && next < SHADE_COUNT) retargetLeg(index, next, ORIGIN_NONE, preset);
}

void GrayModel::commit(const bool driveDone, const bool followDone, const GrayPreset& preset) {
  if (!valid() || !driveDone) return;
  const size_t count = pixelCount(width_, height_);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* pixel = pixelAt(i);
    if (dosesOf(pixel) == 0) continue;
    // The direction and the follow-up decision are buildMasks', read from the state it read.
    const bool lighten = lightens(pixel);
    const bool again = followDone && followsUp(pixel, preset);
    deliverDose(i, lighten, preset);
    if (again) deliverDose(i, lighten, preset);
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
  return valid() && inside(x, y) ? dosesOf(pixelAt(indexOf(x, y))) : 0;
}

uint8_t GrayModel::target(const int x, const int y) const {
  return valid() && inside(x, y) ? targetOf(pixelAt(indexOf(x, y))[SHADES_BYTE]) : 0;
}

uint8_t GrayModel::nextTarget(const int x, const int y) const {
  return valid() && inside(x, y) ? nextOf(pixelAt(indexOf(x, y))) : ORIGIN_NONE;
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
    if (dosesOf(pixelAt(i)) != 0) ++active;
  }
  return active;
}

}  // namespace GameDisplayLab

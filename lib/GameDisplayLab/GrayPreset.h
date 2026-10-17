#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// The gray engine's tunable values (the display-lab epic, R3 and R4; plan-dlab-c1-gray-engine.md): InkBoy's dose
// table, hold mask, retarget scaling, history routes and weak mode, plus the scan's frame-rate index and polarity
// swap. Pure C++, pinned by test/game_display_lab/test_gray_preset.cpp. Two starting presets: InkBoy's (the owner's
// 2026-10-16 decision: every number editable on the device, none claimed calibrated), and the UltraChip one from the
// owner's first UC8179 calibration run (the epic's D12), which the lab uses on a UC8179 or UC8279 panel.
namespace GameDisplayLab {

// The four shades, darkest first; a pixel's level is shade * 64 (K = 0, W = 192).
enum class Shade : uint8_t { K = 0, D = 1, L = 2, W = 3 };

constexpr uint8_t SHADE_COUNT = 4;
constexpr uint8_t TRANSITION_COUNT = 12;
constexpr uint8_t ROUTE_COUNT = 6;
constexpr uint8_t DOSE_MIN = 1;
// The UC8179's full white-to-black swing takes 12 one-frame doses (D12), so the cap leaves room above it.
constexpr uint8_t DOSE_MAX = 24;
constexpr uint8_t RETARGET_MIN = 50;
constexpr uint8_t RETARGET_MAX = 150;
constexpr uint8_t RETARGET_STEP = 25;
constexpr int8_t RETURN_DELTA_LIMIT = 11;
constexpr uint8_t REVERSE_PERCENT_MAX = 250;
// historyMode bits: completed returns, then reversals mid-transition.
constexpr uint8_t HISTORY_RETURN = 1;
constexpr uint8_t HISTORY_REVERSE = 2;

// The direction of a retarget: toward a darker or a lighter level than the pixel's.
enum class Toward : uint8_t { Dark, Light };

// The history routes, origin-via-origin, in the preset file's order.
enum class Route : uint8_t { LKL, DKD, DWD, LWL, DLD, LDL };

// Transition index 0..11 in the table order K>D, K>L, K>W, D>K, D>L, D>W, L>K, L>D, L>W, W>K, W>D, W>L.
constexpr uint8_t transitionFrom(const uint8_t index) { return static_cast<uint8_t>(index / 3); }
constexpr uint8_t transitionTo(const uint8_t index) {
  const uint8_t to = static_cast<uint8_t>(index % 3);
  return to < transitionFrom(index) ? to : static_cast<uint8_t>(to + 1);
}
// The index of from > to; from != to.
constexpr uint8_t transitionIndex(const uint8_t from, const uint8_t to) {
  return static_cast<uint8_t>(from * 3 + (to < from ? to : to - 1));
}
// The hold-mask bit of source > target (InkBoy: bit = source * 4 + target).
constexpr uint16_t holdBit(const uint8_t source, const uint8_t target) {
  return static_cast<uint16_t>(1u << (source * 4u + target));
}
// The hold bits of the four diagonal (no-op) transitions, always set.
constexpr uint16_t HOLD_DIAGONAL = 0x8421;

struct GrayPreset {
  // dose[from][to]: one-frame activations from shade `from` to shade `to`, DOSE_MIN..DOSE_MAX; the diagonal is 0.
  uint8_t dose[SHADE_COUNT][SHADE_COUNT];
  // Bit source * 4 + target set: the follow-up scan holds that transition's pixels.
  uint16_t holdMask;
  // Budget of a pixel retargeted mid-transition, percent of the base dose (50..150): toward a darker level than its
  // own, and toward a lighter one (D12: one value overshot toward white while still falling short toward black).
  uint8_t retargetDarkPercent;
  uint8_t retargetLightPercent;
  // Index into the backend's frame-rate list (checked against the backend when loaded).
  uint8_t rateIndex;
  bool swapPolarity;
  // HISTORY_RETURN | HISTORY_REVERSE; 0 = history off (no history array allocated).
  uint8_t historyMode;
  // Per Route: the completed return's dose change (-11..11, 0 = none) and the reversal's percent (0 = follow retarget).
  int8_t returnDelta[ROUTE_COUNT];
  uint8_t reversePercent[ROUTE_COUNT];
  // W > L pixels settled on white darken through the backend's weak row (only when it has one).
  bool weakWhiteToLight;

  // The defaults: InkBoy's dose table, hold 0xc667, retarget 100 % both ways, and routes (LKL -1 / 150 %, the other
  // five 0 / 0), with rate 0 and no swap; history off and weak off are this story's choice (InkBoy ships history
  // "both" and weak "normal"), so both stay opt-in through the preset file.
  static GrayPreset inkBoyDefaults();
  // The UltraChip starting preset (D12): the owner's UC8179 table [3,7,12,7,2,10,7,2,9,12,5,3] in the transition
  // order, and everything else InkBoy's (holds included). Its rate index 0 is the rate the table was measured at: PLL
  // 0x06 on the UC8179 backend (the experimental branch's list {0x06, 0x05, 0x07}). Its 100 % retarget both ways is an
  // uncalibrated placeholder: on the owner's run 150 % overshot toward white, and the shortfall toward black came
  // through the old 12-dose clamp, which the 24 cap lifts, so RETARGET_MAX stays 150 both ways until a run measures
  // the two directions.
  static GrayPreset ultraChipDefaults();

  // Every value within its range: doses DOSE_MIN..DOSE_MAX off the diagonal and 0 on it, diagonal hold bits set, both
  // retargets 50..150, history 0..3, return deltas -11..11, reverse percents 0..250. The rate index is the backend's to
  // check.
  bool valid() const;

  // Transition `index` (0..11): its dose and hold bit.
  uint8_t doseAt(uint8_t index) const;
  bool holdAt(uint8_t index) const;
  // The retarget percent toward `toward`.
  uint8_t retargetPercent(Toward toward) const;
  // The most slots any transition's whole dose run takes from a settled shade: its dose when its hold bit is on (one
  // dose a slot), half of it rounded up when off (the follow-up scan delivers a second).
  uint8_t longestRunSlots() const;

  // The tuning steps (TuningActivity): each clamps where it would leave the range and cycles where it wraps.
  void stepDose(uint8_t index, int delta);          // +-1, clamped DOSE_MIN..DOSE_MAX
  void cycleDose(uint8_t index);                    // +1, DOSE_MAX wraps to DOSE_MIN
  void setHold(uint8_t index, bool hold);           // the bit itself
  void toggleHold(uint8_t index);                   // flips the bit
  void stepRetarget(Toward toward, int direction);  // +-25 by the sign of `direction`, clamped 50..150
  void cycleRetarget(Toward toward);                // +25, 150 wraps to 50
  void stepRate(int delta, uint8_t rateCount);      // +-1, clamped to 0..rateCount - 1
  void cycleRate(uint8_t rateCount);                // +1, the last wraps to 0
  void toggleSwap() { swapPolarity = !swapPolarity; }
};

// Which starting preset a session gets (D12): the UltraChip one on a UC8179 or UC8279 panel, InkBoy's otherwise.
enum class PresetBase : uint8_t { InkBoy, UltraChip };
GrayPreset presetDefaults(PresetBase base);
// For the log: "InkBoy's" or "UltraChip".
const char* presetBaseName(PresetBase base);

// The preset file's text (JSON, docs/crosshatch/display-lab.md): `{"version":1,"base":"inkboy"|"ultrachip",
// "doses":[...12],"hold":[...12],"retarget_dark_percent":N,"retarget_light_percent":N,"rate":N,"swap_polarity":B,
// "history":N,"return_delta":[...6],"reverse_percent":[...6],"weak_white_to_light":B}`, `base` being the defaults
// the session started from. Writes it and a terminating NUL into `out`; returns its length, or 0 (and `out` is the
// empty string, when it has any room) when `capacity` is too small.
size_t encodeGrayPreset(const GrayPreset& preset, PresetBase base, char* out, size_t capacity);

enum class GrayPresetLoad : uint8_t { Missing, Loaded, Malformed };

// What loadGrayPreset saw besides the values (all false and empty unless Loaded).
struct GrayPresetLoadInfo {
  // The directions the older `retarget_percent` key filled (those the text did not set with their own key).
  bool legacyDark = false;
  bool legacyLight = false;
  // The file's `base`, when it has the key with a value naming one (files saved before it existed have none).
  std::optional<PresetBase> base;
  // The file has a `base` key whose value names no base: ignored (the caller logs it).
  bool unknownBase = false;
};

// Reads `length` bytes of `text`, a std-only JSON subset (objects, arrays, integers, booleans, strings; unknown keys
// and their values, null included, are skipped). Missing when `text` is null; Malformed on a parse error, a known key
// of the wrong type or out of range, an array of the wrong length, or
// `version` absent or not 1; absent keys keep `defaults`. The older single key `retarget_percent` (50..150) is still
// read: each direction without its own key in the text takes its value, whatever the keys' order. `*info`, when
// given, says which directions that filled and the file's base (`base` is informational: a value other than "inkboy"
// or "ultrachip" is skipped and reported as unknownBase). `out` is the read preset when Loaded and `defaults`
// otherwise.
GrayPresetLoad loadGrayPreset(const char* text, size_t length, const GrayPreset& defaults, GrayPreset& out,
                              GrayPresetLoadInfo* info = nullptr);

}  // namespace GameDisplayLab

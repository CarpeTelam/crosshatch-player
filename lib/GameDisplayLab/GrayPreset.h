#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// The gray engine's tunable values (the display-lab epic, R3 and R4; plan-dlab-c1-gray-engine.md): InkBoy's dose
// table, hold mask, retarget scaling, history routes and weak mode, plus the scan's frame-rate index and polarity
// swap, over seven levels since story dlab-g2 (the epic's D14). Pure C++, pinned by
// test/game_display_lab/test_gray_preset.cpp. Two starting presets: InkBoy's (the owner's 2026-10-16 decision: every
// number editable on the device, none claimed calibrated), and the UltraChip one read off the owner's ladder photos
// (D14), which the lab uses on a UC8179 or UC8279 panel.
namespace GameDisplayLab {

// The seven levels, darkest first (the epic's D14): a pixel's level is shade * 32 (0 black .. 192 white), so InkBoy's
// four shades K, D, L, W sit on levels 0, 2, 4, 6 and keep their names; the grays 1, 3 (the 50 % gray) and 5 are
// static_cast<Shade>(level). TRANSPARENT is the 3-bit field's spare value: a level-coded bitmap pixel at 7 is not
// retargeted (GrayModel::retargetBitmap), and no table of the preset holds it.
enum class Shade : uint8_t { K = 0, D = 2, L = 4, W = 6, TRANSPARENT = 7 };

constexpr uint8_t SHADE_COUNT = 7;
constexpr uint8_t TRANSITION_COUNT = SHADE_COUNT * (SHADE_COUNT - 1);  // 42
// The history routes, one per direction a pixel returns to its origin in (indexed by Toward).
constexpr uint8_t ROUTE_COUNT = 2;
// A transition without a via level (GrayPreset::via).
constexpr uint8_t VIA_NONE = 7;
// The preset file's version 1 layout (four shades, twelve transitions, six routes), still read.
constexpr uint8_t V1_SHADE_COUNT = 4;
constexpr uint8_t V1_TRANSITION_COUNT = 12;
constexpr uint8_t V1_ROUTE_COUNT = 6;
constexpr uint8_t DOSE_MIN = 1;
// The UC8179's full white-to-black swing takes 12 one-frame doses (D12), so the cap leaves room above it.
constexpr uint8_t DOSE_MAX = 24;
constexpr uint8_t RETARGET_MIN = 50;
constexpr uint8_t RETARGET_MAX = 150;
constexpr uint8_t RETARGET_STEP = 25;
constexpr int8_t RETURN_DELTA_LIMIT = 11;
constexpr uint8_t REVERSE_PERCENT_MAX = 250;
constexpr uint8_t REVERSE_PERCENT_STEP = 25;
// historyMode bits: completed returns, then reversals mid-transition.
constexpr uint8_t HISTORY_RETURN = 1;
constexpr uint8_t HISTORY_REVERSE = 2;
constexpr uint8_t HISTORY_MAX = HISTORY_RETURN | HISTORY_REVERSE;

// The direction of a retarget: toward a darker or a lighter level than the pixel's. Also the history route's index: a
// pixel returning to its origin takes the route of the direction it returns in.
enum class Toward : uint8_t { Dark, Light };
constexpr size_t routeIndex(const Toward toward) { return static_cast<size_t>(toward); }

// Transition index 0..41 in the table order: from 0..6, then to != from ascending (0>1, 0>2, .., 0>6, 1>0, 1>2, ..,
// 6>5), the preset file's and Tuning's order.
constexpr uint8_t transitionFrom(const uint8_t index) { return static_cast<uint8_t>(index / (SHADE_COUNT - 1)); }
constexpr uint8_t transitionTo(const uint8_t index) {
  const uint8_t to = static_cast<uint8_t>(index % (SHADE_COUNT - 1));
  return to < transitionFrom(index) ? to : static_cast<uint8_t>(to + 1);
}
// The index of from > to; from != to.
constexpr uint8_t transitionIndex(const uint8_t from, const uint8_t to) {
  return static_cast<uint8_t>(from * (SHADE_COUNT - 1) + (to < from ? to : to - 1));
}
// The hold-mask bit of source > target (InkBoy's source * 4 + target, generalised: source * 7 + target, 49 bits).
constexpr uint64_t holdBit(const uint8_t source, const uint8_t target) {
  return uint64_t{1} << (source * SHADE_COUNT + target);
}
constexpr uint64_t holdDiagonal() {
  uint64_t bits = 0;
  for (uint8_t s = 0; s < SHADE_COUNT; ++s) bits |= holdBit(s, s);
  return bits;
}
// The hold bits of the seven diagonal (no-op) transitions, always set; every bit a transition can have.
constexpr uint64_t HOLD_DIAGONAL = holdDiagonal();
constexpr uint64_t HOLD_ALL = (uint64_t{1} << (SHADE_COUNT * SHADE_COUNT)) - 1;

struct GrayPreset {
  // dose[from][to]: one-frame activations from level `from` to level `to`, DOSE_MIN..DOSE_MAX; the diagonal is 0.
  uint8_t dose[SHADE_COUNT][SHADE_COUNT];
  // via[from][to]: VIA_NONE, or a level the transition passes through (story dlab-g2, D14: one pulse from black already
  // passes level 2, so 0>1 runs 0>2 with dose[0][2], then 2>1 with dose[2][1]; GrayModel::retarget). VIA_NONE on the
  // diagonal; never `from` or `to`.
  uint8_t via[SHADE_COUNT][SHADE_COUNT];
  // Bit source * 7 + target set (holdBit): the follow-up scan holds that transition's pixels. Diagonal bits always set,
  // no bit past HOLD_ALL.
  uint64_t holdMask;
  // Budget of a pixel retargeted mid-transition, percent of the base dose (50..150): toward a darker level than its
  // own, and toward a lighter one (D12: one value overshot toward white while still falling short toward black).
  uint8_t retargetDarkPercent;
  uint8_t retargetLightPercent;
  // Index into the backend's frame-rate list (checked against the backend when loaded).
  uint8_t rateIndex;
  bool swapPolarity;
  // HISTORY_RETURN | HISTORY_REVERSE; 0 = history off (no history array allocated).
  uint8_t historyMode;
  // Per route (routeIndex: a return toward dark, toward light): the completed return's dose change (-11..11, 0 = none)
  // and the reversal's percent (0 = follow retarget).
  int8_t returnDelta[ROUTE_COUNT];
  uint8_t reversePercent[ROUTE_COUNT];
  // W > L (6 > 4) pixels settled on white darken through the backend's weak row (only when it has one).
  bool weakWhiteToLight;

  // The defaults: InkBoy's four-shade dose table, hold 0xc667 and routes (L>K>L -1 / 150 %, the other five 0 / 0)
  // expanded to seven levels as a version 1 file is (expandVersion1Doses, expandVersion1Holds, version1RouteFor), no
  // via, retarget 100 % both ways, rate 0 and no swap; history off and weak off are dlab-c1's
  // choice (InkBoy ships history "both" and weak "normal"), so both stay opt-in.
  static GrayPreset inkBoyDefaults();
  // The UltraChip starting preset: the seven-level table the orchestrator read off the owner's ladder photos (D14,
  // 2026-10-19: rate index 0, one pulse per slot, every hold on), its one via (0>1 through 2), and InkBoy's routes,
  // retargets, history and weak mode. Its rate index 0 is PLL 0x06 on the UC8179 backend (the experimental branch's
  // list {0x06, 0x05, 0x07}).
  static GrayPreset ultraChipDefaults();

  // Every value within its range: doses DOSE_MIN..DOSE_MAX off the diagonal and 0 on it, vias VIA_NONE on the diagonal
  // and VIA_NONE or a level other than both ends off it, diagonal hold bits set and none past HOLD_ALL, both retargets
  // 50..150, history 0..3, return deltas -11..11, reverse percents 0..250. The rate index is the backend's to check.
  bool valid() const;

  // Transition `index` (0..41): its dose, hold bit and via (VIA_NONE: none).
  uint8_t doseAt(uint8_t index) const;
  bool holdAt(uint8_t index) const;
  uint8_t viaAt(uint8_t index) const;
  // The retarget percent toward `toward`.
  uint8_t retargetPercent(Toward toward) const;
  // The most slots any transition's whole dose run takes from a settled level: per leg, its dose when its hold bit is
  // on (one dose a slot), half of it rounded up when off (the follow-up scan delivers a second); a transition with a
  // via runs both its legs.
  uint8_t longestRunSlots() const;

  // The tuning steps (TuningActivity): each clamps where it would leave the range and cycles where it wraps.
  void stepDose(uint8_t index, int delta);             // +-1, clamped DOSE_MIN..DOSE_MAX
  void cycleDose(uint8_t index);                       // +1, DOSE_MAX wraps to DOSE_MIN
  void setHold(uint8_t index, bool hold);              // the bit itself
  void toggleHold(uint8_t index);                      // flips the bit
  void stepVia(uint8_t index, int direction);          // none, then the levels other than both ends ascending; clamped
  void cycleVia(uint8_t index);                        // the same order, the last wraps to none
  void stepRetarget(Toward toward, int direction);     // +-25 by the sign of `direction`, clamped 50..150
  void cycleRetarget(Toward toward);                   // +25, 150 wraps to 50
  void stepHistory(int direction);                     // +-1, clamped 0..3
  void cycleHistory();                                 // +1, 3 wraps to 0
  void stepReturnDelta(Toward toward, int direction);  // +-1, clamped -11..11
  void cycleReturnDelta(Toward toward);                // +1, 11 wraps to -11
  void stepReversePercent(Toward toward, int direction);  // +-25, clamped 0..250
  void cycleReversePercent(Toward toward);                // +25, 250 wraps to 0
  void stepRate(int delta, uint8_t rateCount);            // +-1, clamped to 0..rateCount - 1
  void cycleRate(uint8_t rateCount);                      // +1, the last wraps to 0
  void toggleSwap() { swapPolarity = !swapPolarity; }
};

// The version 1 file's four-shade values expanded to seven levels (story dlab-g2): the shades land on levels 0, 2, 4,
// 6. A dose into or out of an odd level is the mean of the two anchor transitions around it (the diagonal counting 0),
// one between two odd levels the mean of its four, rounded up once. `v1` is dose[from][to] over K, D, L, W.
void expandVersion1Doses(const uint8_t v1[V1_SHADE_COUNT][V1_SHADE_COUNT], uint8_t out[SHADE_COUNT][SHADE_COUNT]);
// A 16-bit version 1 hold mask (bit source * 4 + target) as seven levels: each transition takes the bit of its nearest
// anchor pair, an odd level rounding away from the transition's other end (0>1 takes 0>2, 3>1 takes 4>0, 1>3 takes
// 0>4), so the pair is always an anchor transition in the same direction. Diagonal bits set.
uint64_t expandVersion1Holds(uint16_t v1);
// Version 1's six routes (L>K>L, D>K>D, D>W>D, L>W>L, D>L>D, L>D>L, the file's order) as the two directional ones:
// the index of the version 1 route whose values a direction takes. Toward light takes L>K>L's (a return from black, the
// story's reading), toward dark its mirror D>W>D's (a return from white).
constexpr uint8_t version1RouteFor(const Toward toward) { return toward == Toward::Light ? 0 : 2; }

// Which starting preset a session gets (D12): the UltraChip one on a UC8179 or UC8279 panel, InkBoy's otherwise.
enum class PresetBase : uint8_t { InkBoy, UltraChip };
GrayPreset presetDefaults(PresetBase base);
// For the log: "InkBoy's" or "UltraChip".
const char* presetBaseName(PresetBase base);

// The preset file's text (JSON, docs/crosshatch/display-lab.md), version 2: `{"version":2,"base":"inkboy"|"ultrachip",
// "doses":[...42],"hold":[...42],"via":[...42],"retarget_dark_percent":N,"retarget_light_percent":N,"rate":N,
// "swap_polarity":B,"history":N,"return_delta":[dark,light],"reverse_percent":[dark,light],"weak_white_to_light":B}`,
// the 42-entry arrays in the transition order, a via of -1 meaning none; `base` being the defaults the session started
// from. Writes it and a terminating NUL into `out`; returns its length, or 0 (and `out` is the empty string, when it
// has any room) when `capacity` is too small.
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
  // A version 1 file: its four-shade arrays were expanded to seven levels (the caller logs it).
  bool expandedVersion1 = false;
};

// Reads `length` bytes of `text`, a std-only JSON subset (objects, arrays, integers, booleans, strings; unknown keys
// and their values, null included, are skipped). Missing when `text` is null; Malformed on a parse error, a known key
// of the wrong type or out of range (a via of 7 or above included), an array whose length is not its version's
// (version 1: doses and hold 12, return_delta and reverse_percent 6, no via; version 2: 42, 42, 42, 2 and 2), or
// `version` absent or not 1 or 2; absent keys keep `defaults`. Keys may come in any order. A version 1 file's arrays
// are expanded (expandVersion1Doses, expandVersion1Holds, version1RouteFor). The older single key
// `retarget_percent` (50..150) is still read: each direction without its own key in the text takes its value, whatever
// the keys' order. `*info`, when given, says which directions that filled, the file's base (`base` is informational: a
// value other than "inkboy" or "ultrachip" is skipped and reported as unknownBase) and whether it was version 1. `out`
// is the read preset when Loaded and `defaults` otherwise; `out` and `defaults` are two different presets (the text is
// read into `out` directly).
GrayPresetLoad loadGrayPreset(const char* text, size_t length, const GrayPreset& defaults, GrayPreset& out,
                              GrayPresetLoadInfo* info = nullptr);

}  // namespace GameDisplayLab

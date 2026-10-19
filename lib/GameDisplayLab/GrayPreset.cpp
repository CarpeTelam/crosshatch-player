#include "GrayPreset.h"

#include <cstdio>
#include <cstring>

namespace GameDisplayLab {

namespace {

// InkBoy's default dose table (presets/grayscale/default.json, the digest's table), dose[from][to] over K, D, L, W:
// the four-shade table inkBoyDefaults() expands to seven levels.
constexpr uint8_t INKBOY_DOSE[V1_SHADE_COUNT][V1_SHADE_COUNT] = {
    {0, 2, 4, 7},  // K > D, L, W
    {4, 0, 1, 6},  // D > K, L, W
    {4, 1, 0, 5},  // L > K, D, W
    {7, 3, 2, 0},  // W > K, D, L
};
// InkBoy's HOLD mask (gray_config.h:48-49), bit source * 4 + target over K, D, L, W.
constexpr uint16_t INKBOY_HOLD = 0xc667;
// The version 1 file's diagonal hold bits (K>K, D>D, L>L, W>W).
constexpr uint16_t V1_HOLD_DIAGONAL = 0x8421;
// The seven-level UltraChip table (D14, 2026-10-19: read off the owner's two ladders at rate index 0, one pulse per
// slot, every hold on; the four-shade anchors are the Slow-validated prediction), dose[from][to]. 0>1 runs through its
// via (dose[0][2], then dose[2][1]); its own entry is the file's 1, unused while the via is set.
constexpr uint8_t ULTRACHIP_DOSE[SHADE_COUNT][SHADE_COUNT] = {
    {0, 1, 1, 2, 2, 4, 9},   // 0 > 1..6
    {5, 0, 1, 1, 2, 4, 8},   // 1 > 0, 2..6
    {7, 2, 0, 1, 1, 3, 8},   // 2 > 0, 1, 3..6
    {8, 3, 1, 0, 1, 3, 7},   // 3 > 0..2, 4..6
    {9, 4, 2, 1, 0, 2, 7},   // 4 > 0..3, 5, 6
    {10, 5, 3, 2, 1, 0, 5},  // 5 > 0..4, 6
    {12, 7, 5, 4, 3, 2, 0},  // 6 > 0..5
};
// D14's one via: 0>1 goes to level 2 (one pulse) and back (two).
constexpr uint8_t ULTRACHIP_VIA_FROM = 0;
constexpr uint8_t ULTRACHIP_VIA_TO = 1;
constexpr uint8_t ULTRACHIP_VIA = 2;
constexpr uint8_t INKBOY_RETARGET = 100;
// Version 1 routes LKL, DKD, DWD, LWL, DLD, LDL: L>K>L returns one dose short and reverses at 150 %; the rest are
// neutral.
constexpr int8_t INKBOY_RETURN_DELTA[V1_ROUTE_COUNT] = {-1, 0, 0, 0, 0, 0};
constexpr uint8_t INKBOY_REVERSE_PERCENT[V1_ROUTE_COUNT] = {150, 0, 0, 0, 0, 0};

constexpr int FILE_VERSION = 2;
constexpr int FILE_VERSION_1 = 1;
constexpr uint8_t RATE_MAX = 255;
// The file's via for none.
constexpr int FILE_VIA_NONE = -1;
// Nesting an unknown value may have before the reader calls the text malformed (bounds the recursion).
constexpr int MAX_DEPTH = 16;

uint8_t clampDose(const int value) {
  if (value < DOSE_MIN) return DOSE_MIN;
  if (value > DOSE_MAX) return DOSE_MAX;
  return static_cast<uint8_t>(value);
}

// The retarget percent toward `toward`, for a const or a mutable preset (`auto&` keeps the constness).
template <typename Preset>
auto& retargetField(Preset& preset, const Toward toward) {
  switch (toward) {
    case Toward::Dark:
      return preset.retargetDarkPercent;
    case Toward::Light:
      return preset.retargetLightPercent;
  }
  return preset.retargetDarkPercent;
}

// The `base` key's value in the preset file.
const char* baseKeyOf(const PresetBase base) {
  switch (base) {
    case PresetBase::InkBoy:
      return "inkboy";
    case PresetBase::UltraChip:
      return "ultrachip";
  }
  return "inkboy";
}

bool retargetInRange(const uint8_t percent) { return percent >= RETARGET_MIN && percent <= RETARGET_MAX; }

// --- encoding ---------------------------------------------------------------------------------------------------

// Appends to a caller buffer; `failed` once anything did not fit with its NUL.
struct Writer {
  char* out;
  size_t capacity;
  size_t length = 0;
  bool failed = false;

  void text(const char* s) {
    const size_t n = strlen(s);
    if (failed || length + n + 1 > capacity) {
      failed = true;
      return;
    }
    memcpy(out + length, s, n);
    length += n;
    out[length] = '\0';
  }
  void number(const int value) {
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", value);
    text(digits);
  }
  void boolean(const bool value) { text(value ? "true" : "false"); }
};

// --- decoding ---------------------------------------------------------------------------------------------------

// A cursor over the preset text; every parse step returns false on malformed input.
class Reader {
 public:
  Reader(const char* text, const size_t length) : p_(text), end_(text + length) {}

  void skipSpace() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }
  bool atEnd() {
    skipSpace();
    return p_ == end_;
  }
  // Consumes `c` after any whitespace.
  bool take(const char c) {
    skipSpace();
    if (p_ == end_ || *p_ != c) return false;
    ++p_;
    return true;
  }
  // A string into `out` (NUL-terminated; `overlong` when it did not fit, so it matches no key).
  bool string(char* out, const size_t capacity, bool& overlong) {
    overlong = false;
    if (!take('"')) return false;
    size_t n = 0;
    while (p_ < end_ && *p_ != '"') {
      char c = *p_++;
      if (c == '\\') {
        if (p_ == end_) return false;
        c = *p_++;  // an escape is kept as its letter: no key needs one, and a skipped string only needs its end
      }
      if (n + 1 < capacity) {
        out[n++] = c;
      } else {
        overlong = true;
      }
    }
    if (p_ == end_) return false;
    ++p_;  // the closing quote
    out[n] = '\0';
    return true;
  }

  // A JSON integer: an optional minus and at least one digit, no fraction or exponent. Values past +-1,000,000 read
  // as +-1,000,000, which no range accepts.
  bool integer(int& out) {
    skipSpace();
    bool negative = false;
    if (p_ < end_ && *p_ == '-') {
      negative = true;
      ++p_;
    }
    if (p_ == end_ || *p_ < '0' || *p_ > '9') return false;
    long value = 0;
    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') {
      if (value < 1000000) value = value * 10 + (*p_ - '0');
      ++p_;
    }
    if (value > 1000000) value = 1000000;
    if (p_ < end_ && (*p_ == '.' || *p_ == 'e' || *p_ == 'E')) return false;  // not an integer
    out = static_cast<int>(negative ? -value : value);
    return true;
  }

  bool boolean(bool& out) {
    skipSpace();
    if (word("true")) {
      out = true;
      return true;
    }
    if (word("false")) {
      out = false;
      return true;
    }
    return false;
  }

  // Any JSON value, read and discarded: an unknown key's.
  bool skipValue(const int depth) {
    if (depth > MAX_DEPTH) return false;
    skipSpace();
    if (p_ == end_) return false;
    switch (*p_) {
      case '{':
        return skipContainer('}', depth, true);
      case '[':
        return skipContainer(']', depth, false);
      case '"': {
        char unused[1];
        bool overlong = false;
        return string(unused, sizeof(unused), overlong);
      }
      case 't':
        return word("true");
      case 'f':
        return word("false");
      case 'n':
        return word("null");
      default:
        return skipNumber();
    }
  }

 private:
  bool word(const char* w) {
    const size_t n = strlen(w);
    if (static_cast<size_t>(end_ - p_) < n || memcmp(p_, w, n) != 0) return false;
    p_ += n;
    return true;
  }

  bool skipNumber() {
    const char* start = p_;
    while (p_ < end_ &&
           ((*p_ >= '0' && *p_ <= '9') || *p_ == '-' || *p_ == '+' || *p_ == '.' || *p_ == 'e' || *p_ == 'E')) {
      ++p_;
    }
    return p_ != start;
  }

  // An object (`keyed`) or array after its opening bracket, through `close`.
  bool skipContainer(const char close, const int depth, const bool keyed) {
    ++p_;  // the opening bracket
    if (take(close)) return true;
    do {
      if (keyed) {
        char unused[1];
        bool overlong = false;
        if (!string(unused, sizeof(unused), overlong) || !take(':')) return false;
      }
      if (!skipValue(depth + 1)) return false;
    } while (take(','));
    return take(close);
  }

  const char* p_;
  const char* end_;
};

bool intInRange(Reader& reader, const int low, const int high, int& out) {
  return reader.integer(out) && out >= low && out <= high;
}

// `[a, b, ...]` of at most `capacity` integers in low..high, each handed to `sink(index, value)` as it is read (no
// buffer: the reader's callers stay under the 256-byte stack rule); `count` is how many.
template <typename Sink>
bool intList(Reader& reader, const size_t capacity, const int low, const int high, uint8_t& count, Sink sink) {
  count = 0;
  if (!reader.take('[')) return false;
  if (reader.take(']')) return true;
  do {
    int value = 0;
    if (count >= capacity || !intInRange(reader, low, high, value)) return false;
    sink(count, value);
    ++count;
  } while (reader.take(','));
  return reader.take(']');
}

// `[true, false, ...]` of at most `capacity` (<= 64) booleans into bit i of `bits`; `count` is how many.
bool boolList(Reader& reader, uint64_t& bits, const size_t capacity, uint8_t& count) {
  count = 0;
  bits = 0;
  if (!reader.take('[')) return false;
  if (reader.take(']')) return true;
  do {
    bool value = false;
    if (count >= capacity || !reader.boolean(value)) return false;
    if (value) bits |= uint64_t{1} << count;
    ++count;
  } while (reader.take(','));
  return reader.take(']');
}

// What loadGrayPreset learned besides the values: the version, which retarget keys were present, the older single
// retarget key's value (-1: absent), and each array's length (0: absent) with a version 1 file's four-shade values,
// all applied after the whole object so the order of the keys does not matter.
struct ReadState {
  int version = 0;
  bool sawDark = false;
  bool sawLight = false;
  int legacyRetarget = -1;
  std::optional<PresetBase> base;
  bool unknownBase = false;
  uint8_t doseCount = 0;
  uint8_t holdCount = 0;
  uint8_t viaCount = 0;
  uint8_t deltaCount = 0;
  uint8_t percentCount = 0;
  // A version 1 file's arrays (12 doses, 12 hold bits, six routes), kept until the version is known.
  uint8_t v1Dose[V1_TRANSITION_COUNT] = {};
  uint16_t v1Hold = 0;
  int8_t v1Delta[V1_ROUTE_COUNT] = {};
  uint8_t v1Percent[V1_ROUTE_COUNT] = {};
};

// Version 1's transition index (0..11, K>D, K>L, K>W, D>K, ..., W>L): its from and to shade (0..3).
constexpr uint8_t v1From(const uint8_t index) { return static_cast<uint8_t>(index / (V1_SHADE_COUNT - 1)); }
constexpr uint8_t v1To(const uint8_t index) {
  const uint8_t to = static_cast<uint8_t>(index % (V1_SHADE_COUNT - 1));
  return to < v1From(index) ? to : static_cast<uint8_t>(to + 1);
}

// The arrays a route list holds: version 1's six routes, or the two directional ones.
bool routeCountFits(const uint8_t count) { return count == V1_ROUTE_COUNT || count == ROUTE_COUNT; }
bool transitionCountFits(const uint8_t count) { return count == V1_TRANSITION_COUNT || count == TRANSITION_COUNT; }

// One known key's value into `preset` (a version 1 array into `state`); an unknown key's value is skipped. False when
// the value is malformed. An array's length picks its layout here; loadGrayPreset checks it against the version.
bool readValue(Reader& reader, const char* key, GrayPreset& preset, ReadState& state) {
  int number = 0;
  if (strcmp(key, "version") == 0) {
    if (!reader.integer(number) || (number != FILE_VERSION_1 && number != FILE_VERSION)) return false;
    state.version = number;
    return true;
  }
  if (strcmp(key, "base") == 0) {
    // Informational: a value that names no base (or is no string) is reported and skipped, never fatal.
    char value[12];
    bool overlong = false;
    if (!reader.string(value, sizeof(value), overlong)) {
      state.unknownBase = true;
      return reader.skipValue(1);  // not a string (an unterminated one fails here too: a parse error)
    }
    for (const PresetBase base : {PresetBase::InkBoy, PresetBase::UltraChip}) {
      if (!overlong && strcmp(value, baseKeyOf(base)) == 0) {
        state.base = base;
        state.unknownBase = false;
        return true;
      }
    }
    state.base.reset();
    state.unknownBase = true;
    return true;
  }
  if (strcmp(key, "doses") == 0) {
    // Written to both layouts as read: a version 1 file's twelve go to its own array too, and its expansion later
    // overwrites the whole table.
    const auto sink = [&preset, &state](const uint8_t i, const int value) {
      if (i < V1_TRANSITION_COUNT) state.v1Dose[i] = static_cast<uint8_t>(value);
      preset.dose[transitionFrom(i)][transitionTo(i)] = static_cast<uint8_t>(value);
    };
    return intList(reader, TRANSITION_COUNT, DOSE_MIN, DOSE_MAX, state.doseCount, sink) &&
           transitionCountFits(state.doseCount);
  }
  if (strcmp(key, "hold") == 0) {
    uint64_t bits = 0;
    if (!boolList(reader, bits, TRANSITION_COUNT, state.holdCount)) return false;
    if (!transitionCountFits(state.holdCount)) return false;
    if (state.holdCount == V1_TRANSITION_COUNT) {
      state.v1Hold = V1_HOLD_DIAGONAL;
      for (uint8_t i = 0; i < V1_TRANSITION_COUNT; ++i) {
        if ((bits >> i) & 1u) state.v1Hold = static_cast<uint16_t>(state.v1Hold | (1u << (v1From(i) * 4u + v1To(i))));
      }
      return true;
    }
    preset.holdMask = HOLD_DIAGONAL;
    for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) preset.setHold(i, ((bits >> i) & 1u) != 0);
    return true;
  }
  if (strcmp(key, "via") == 0) {
    // -1 for none, else a level 0..6: 7 (TRANSPARENT) or above is never a level in a table (story dlab-g2, item 8).
    const auto sink = [&preset](const uint8_t i, const int value) {
      preset.via[transitionFrom(i)][transitionTo(i)] = value == FILE_VIA_NONE ? VIA_NONE : static_cast<uint8_t>(value);
    };
    return intList(reader, TRANSITION_COUNT, FILE_VIA_NONE, SHADE_COUNT - 1, state.viaCount, sink) &&
           state.viaCount == TRANSITION_COUNT;
  }
  if (strcmp(key, "retarget_dark_percent") == 0) {
    if (!intInRange(reader, RETARGET_MIN, RETARGET_MAX, number)) return false;
    preset.retargetDarkPercent = static_cast<uint8_t>(number);
    state.sawDark = true;
    return true;
  }
  if (strcmp(key, "retarget_light_percent") == 0) {
    if (!intInRange(reader, RETARGET_MIN, RETARGET_MAX, number)) return false;
    preset.retargetLightPercent = static_cast<uint8_t>(number);
    state.sawLight = true;
    return true;
  }
  if (strcmp(key, "retarget_percent") == 0) {
    // The file format before the split (dlab-c1, dlab-c2): one value for both directions.
    if (!intInRange(reader, RETARGET_MIN, RETARGET_MAX, number)) return false;
    state.legacyRetarget = number;
    return true;
  }
  if (strcmp(key, "rate") == 0) {
    if (!intInRange(reader, 0, RATE_MAX, number)) return false;
    preset.rateIndex = static_cast<uint8_t>(number);
    return true;
  }
  if (strcmp(key, "swap_polarity") == 0) return reader.boolean(preset.swapPolarity);
  if (strcmp(key, "history") == 0) {
    if (!intInRange(reader, 0, HISTORY_MAX, number)) return false;
    preset.historyMode = static_cast<uint8_t>(number);
    return true;
  }
  if (strcmp(key, "return_delta") == 0) {
    // Both layouts as read, as the doses: a version 1 file's six are collapsed later.
    const auto sink = [&preset, &state](const uint8_t i, const int value) {
      state.v1Delta[i] = static_cast<int8_t>(value);
      if (i < ROUTE_COUNT) preset.returnDelta[i] = static_cast<int8_t>(value);
    };
    return intList(reader, V1_ROUTE_COUNT, -RETURN_DELTA_LIMIT, RETURN_DELTA_LIMIT, state.deltaCount, sink) &&
           routeCountFits(state.deltaCount);
  }
  if (strcmp(key, "reverse_percent") == 0) {
    const auto sink = [&preset, &state](const uint8_t i, const int value) {
      state.v1Percent[i] = static_cast<uint8_t>(value);
      if (i < ROUTE_COUNT) preset.reversePercent[i] = static_cast<uint8_t>(value);
    };
    return intList(reader, V1_ROUTE_COUNT, 0, REVERSE_PERCENT_MAX, state.percentCount, sink) &&
           routeCountFits(state.percentCount);
  }
  if (strcmp(key, "weak_white_to_light") == 0) return reader.boolean(preset.weakWhiteToLight);
  return reader.skipValue(1);
}

// Whether every array present has its version's length (version 1: 12, 12, no via, 6, 6; version 2: 42, 42, 42, 2,
// 2).
bool arraysFitVersion(const ReadState& state) {
  const bool v1 = state.version == FILE_VERSION_1;
  const uint8_t transitions = v1 ? V1_TRANSITION_COUNT : TRANSITION_COUNT;
  const uint8_t routes = v1 ? V1_ROUTE_COUNT : ROUTE_COUNT;
  const auto fits = [](const uint8_t count, const uint8_t wanted) { return count == 0 || count == wanted; };
  if (v1 && state.viaCount != 0) return false;
  return fits(state.doseCount, transitions) && fits(state.holdCount, transitions) &&
         fits(state.viaCount, TRANSITION_COUNT) && fits(state.deltaCount, routes) && fits(state.percentCount, routes);
}

// A version 1 file's arrays, each present one expanded into `preset` (story dlab-g2), and no vias (the file's table was
// never calibrated for one). Returns the routes the collapse dropped with a non-zero value (bit r: version 1 route r).
uint8_t expandVersion1(const ReadState& state, GrayPreset& preset) {
  memset(preset.via, VIA_NONE, sizeof(preset.via));
  if (state.doseCount != 0) {
    uint8_t v1[V1_SHADE_COUNT][V1_SHADE_COUNT] = {};
    for (uint8_t i = 0; i < V1_TRANSITION_COUNT; ++i) v1[v1From(i)][v1To(i)] = state.v1Dose[i];
    expandVersion1Doses(v1, preset.dose);
  }
  if (state.holdCount != 0) preset.holdMask = expandVersion1Holds(state.v1Hold);
  for (const Toward toward : {Toward::Dark, Toward::Light}) {
    const size_t route = routeIndex(toward);
    if (state.deltaCount != 0) preset.returnDelta[route] = state.v1Delta[version1RouteFor(toward)];
    if (state.percentCount != 0) preset.reversePercent[route] = state.v1Percent[version1RouteFor(toward)];
  }
  uint8_t dropped = 0;
  for (uint8_t r = 0; r < V1_ROUTE_COUNT; ++r) {
    if (r == version1RouteFor(Toward::Dark) || r == version1RouteFor(Toward::Light)) continue;
    const bool delta = state.deltaCount != 0 && state.v1Delta[r] != 0;
    const bool percent = state.percentCount != 0 && state.v1Percent[r] != 0;
    if (delta || percent) dropped = static_cast<uint8_t>(dropped | (1u << r));
  }
  return dropped;
}

// The steps a via row takes for transition `index`: VIA_NONE, then the levels other than both ends ascending.
constexpr uint8_t VIA_CHOICES = SHADE_COUNT - 1;
uint8_t viaChoice(const uint8_t index, const uint8_t position) {
  if (position == 0) return VIA_NONE;
  const uint8_t from = transitionFrom(index);
  const uint8_t to = transitionTo(index);
  uint8_t seen = 0;
  for (uint8_t level = 0; level < SHADE_COUNT; ++level) {
    if (level == from || level == to) continue;
    if (++seen == position) return level;
  }
  return VIA_NONE;
}
uint8_t viaPosition(const uint8_t index, const uint8_t via) {
  for (uint8_t position = 0; position < VIA_CHOICES; ++position) {
    if (viaChoice(index, position) == via) return position;
  }
  return 0;
}

// `value` + `step`, clamped to low..high.
int clampedStep(const int value, const int step, const int low, const int high) {
  const int next = value + step;
  if (next < low) return low;
  if (next > high) return high;
  return next;
}

// The sign of `direction` times `step`.
int signedStep(const int direction, const int step) { return direction > 0 ? step : (direction < 0 ? -step : 0); }

// One leg's slots from a settled level: its dose when held, else half of it rounded up (the follow-up scan).
unsigned legSlots(const GrayPreset& preset, const uint8_t from, const uint8_t to) {
  const unsigned d = preset.dose[from][to];
  return (preset.holdMask & holdBit(from, to)) != 0 ? d : (d + 1) / 2;
}

}  // namespace

void expandVersion1Doses(const uint8_t v1[V1_SHADE_COUNT][V1_SHADE_COUNT], uint8_t out[SHADE_COUNT][SHADE_COUNT]) {
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      if (from == to) {
        out[from][to] = 0;
        continue;
      }
      // The anchors (version 1 shades) around each end: one for an even level, the two beside an odd one.
      const uint8_t fromLow = from / 2;
      const uint8_t fromHigh = static_cast<uint8_t>((from + 1) / 2);
      const uint8_t toLow = to / 2;
      const uint8_t toHigh = static_cast<uint8_t>((to + 1) / 2);
      unsigned sum = 0;
      unsigned count = 0;
      for (uint8_t a = fromLow; a <= fromHigh; ++a) {
        for (uint8_t b = toLow; b <= toHigh; ++b) {
          sum += a == b ? 0u : v1[a][b];  // a diagonal anchor transition moves nothing
          ++count;
        }
      }
      out[from][to] = static_cast<uint8_t>((sum + count - 1) / count);
    }
  }
}

uint64_t expandVersion1Holds(const uint16_t v1) {
  uint64_t mask = HOLD_DIAGONAL;
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      if (from == to) continue;
      // The candidates: an anchor (version 1 shade) of each end, running the same way; the one spanning the fewest
      // shades, then the one whose source anchor lies farther from `to`.
      int bestA = -1;
      int bestB = -1;
      for (int a = from / 2; a <= (from + 1) / 2; ++a) {
        for (int b = to / 2; b <= (to + 1) / 2; ++b) {
          if (a == b || (a < b) != (from < to)) continue;
          const int span = a < b ? b - a : a - b;
          const int bestSpan = bestA < 0 ? 0 : (bestA < bestB ? bestB - bestA : bestA - bestB);
          const int away = 2 * a > to ? 2 * a - to : to - 2 * a;
          const int bestAway = 2 * bestA > to ? 2 * bestA - to : to - 2 * bestA;
          if (bestA < 0 || span < bestSpan || (span == bestSpan && away > bestAway)) {
            bestA = a;
            bestB = b;
          }
        }
      }
      if ((v1 >> (bestA * 4 + bestB)) & 1u) mask |= holdBit(from, to);
    }
  }
  return mask;
}

void holdRowsOf(const GrayPreset& preset, uint8_t rows[SHADE_COUNT]) {
  for (uint8_t source = 0; source < SHADE_COUNT; ++source) {
    rows[source] = static_cast<uint8_t>((preset.holdMask >> (source * SHADE_COUNT)) & ((1u << SHADE_COUNT) - 1));
  }
}

const char* version1RouteName(const uint8_t route) {
  constexpr const char* NAMES[V1_ROUTE_COUNT] = {"4>0>4", "2>0>2", "2>6>2", "4>6>4", "2>4>2", "4>2>4"};
  return route < V1_ROUTE_COUNT ? NAMES[route] : "?";
}

GrayPreset GrayPreset::inkBoyDefaults() {
  GrayPreset preset{};
  expandVersion1Doses(INKBOY_DOSE, preset.dose);
  memset(preset.via, VIA_NONE, sizeof(preset.via));
  preset.holdMask = expandVersion1Holds(INKBOY_HOLD);
  preset.retargetDarkPercent = INKBOY_RETARGET;
  preset.retargetLightPercent = INKBOY_RETARGET;
  preset.rateIndex = 0;
  preset.swapPolarity = false;
  preset.historyMode = 0;
  for (const Toward toward : {Toward::Dark, Toward::Light}) {
    preset.returnDelta[routeIndex(toward)] = INKBOY_RETURN_DELTA[version1RouteFor(toward)];
    preset.reversePercent[routeIndex(toward)] = INKBOY_REVERSE_PERCENT[version1RouteFor(toward)];
  }
  preset.weakWhiteToLight = false;
  return preset;
}

GrayPreset GrayPreset::ultraChipDefaults() {
  GrayPreset preset = inkBoyDefaults();
  memcpy(preset.dose, ULTRACHIP_DOSE, sizeof(preset.dose));
  memset(preset.via, VIA_NONE, sizeof(preset.via));
  preset.via[ULTRACHIP_VIA_FROM][ULTRACHIP_VIA_TO] = ULTRACHIP_VIA;
  preset.holdMask = HOLD_ALL;  // D14: every hold on, one pulse per slot
  return preset;
}

GrayPreset presetDefaults(const PresetBase base) {
  switch (base) {
    case PresetBase::InkBoy:
      return GrayPreset::inkBoyDefaults();
    case PresetBase::UltraChip:
      return GrayPreset::ultraChipDefaults();
  }
  return GrayPreset::inkBoyDefaults();
}

const char* presetBaseName(const PresetBase base) {
  switch (base) {
    case PresetBase::InkBoy:
      return "InkBoy's";
    case PresetBase::UltraChip:
      return "UltraChip";
  }
  return "InkBoy's";
}

bool GrayPreset::valid() const {
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      const uint8_t d = dose[from][to];
      if (from == to ? d != 0 : (d < DOSE_MIN || d > DOSE_MAX)) return false;
      const uint8_t v = via[from][to];
      if (v != VIA_NONE && (from == to || v >= SHADE_COUNT || v == from || v == to)) return false;
    }
  }
  if ((holdMask & HOLD_DIAGONAL) != HOLD_DIAGONAL || (holdMask & ~HOLD_ALL) != 0) return false;
  if (!retargetInRange(retargetDarkPercent) || !retargetInRange(retargetLightPercent)) return false;
  if (historyMode > HISTORY_MAX) return false;
  for (size_t i = 0; i < ROUTE_COUNT; ++i) {
    if (returnDelta[i] < -RETURN_DELTA_LIMIT || returnDelta[i] > RETURN_DELTA_LIMIT) return false;
    if (reversePercent[i] > REVERSE_PERCENT_MAX) return false;
  }
  return true;
}

uint8_t GrayPreset::doseAt(const uint8_t index) const { return dose[transitionFrom(index)][transitionTo(index)]; }

bool GrayPreset::holdAt(const uint8_t index) const {
  return (holdMask & holdBit(transitionFrom(index), transitionTo(index))) != 0;
}

uint8_t GrayPreset::viaAt(const uint8_t index) const { return via[transitionFrom(index)][transitionTo(index)]; }

uint8_t GrayPreset::retargetPercent(const Toward toward) const { return retargetField(*this, toward); }

uint8_t GrayPreset::longestRunSlots() const {
  unsigned longest = 0;
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    // A held leg gets one dose a slot; one without its hold bit two (the follow-up scan) while more than one is left,
    // so ceil(dose / 2). A via runs both legs.
    const uint8_t from = transitionFrom(i);
    const uint8_t to = transitionTo(i);
    const uint8_t v = viaAt(i);
    const unsigned slots =
        v < SHADE_COUNT ? legSlots(*this, from, v) + legSlots(*this, v, to) : legSlots(*this, from, to);
    if (slots > longest) longest = slots;
  }
  return static_cast<uint8_t>(longest > 255 ? 255 : longest);
}

void GrayPreset::stepDose(const uint8_t index, const int delta) {
  uint8_t& d = dose[transitionFrom(index)][transitionTo(index)];
  d = clampDose(d + delta);
}

void GrayPreset::cycleDose(const uint8_t index) {
  uint8_t& d = dose[transitionFrom(index)][transitionTo(index)];
  d = d >= DOSE_MAX ? DOSE_MIN : static_cast<uint8_t>(d + 1);
}

void GrayPreset::setHold(const uint8_t index, const bool hold) {
  const uint64_t bit = holdBit(transitionFrom(index), transitionTo(index));
  holdMask = hold ? holdMask | bit : holdMask & ~bit;
}

void GrayPreset::toggleHold(const uint8_t index) { setHold(index, !holdAt(index)); }

void GrayPreset::stepVia(const uint8_t index, const int direction) {
  const int position = clampedStep(viaPosition(index, viaAt(index)), signedStep(direction, 1), 0, VIA_CHOICES - 1);
  via[transitionFrom(index)][transitionTo(index)] = viaChoice(index, static_cast<uint8_t>(position));
}

void GrayPreset::cycleVia(const uint8_t index) {
  const uint8_t position = viaPosition(index, viaAt(index));
  via[transitionFrom(index)][transitionTo(index)] =
      viaChoice(index, static_cast<uint8_t>(position + 1 >= VIA_CHOICES ? 0 : position + 1));
}

void GrayPreset::stepRetarget(const Toward toward, const int direction) {
  uint8_t& percent = retargetField(*this, toward);
  percent =
      static_cast<uint8_t>(clampedStep(percent, signedStep(direction, RETARGET_STEP), RETARGET_MIN, RETARGET_MAX));
}

void GrayPreset::cycleRetarget(const Toward toward) {
  uint8_t& percent = retargetField(*this, toward);
  const int next = percent + RETARGET_STEP;
  percent = next > RETARGET_MAX ? RETARGET_MIN : static_cast<uint8_t>(next);
}

void GrayPreset::stepHistory(const int direction) {
  historyMode = static_cast<uint8_t>(clampedStep(historyMode, signedStep(direction, 1), 0, HISTORY_MAX));
}

void GrayPreset::cycleHistory() {
  historyMode = historyMode >= HISTORY_MAX ? 0 : static_cast<uint8_t>(historyMode + 1);
}

void GrayPreset::stepReturnDelta(const Toward toward, const int direction) {
  int8_t& delta = returnDelta[routeIndex(toward)];
  delta = static_cast<int8_t>(clampedStep(delta, signedStep(direction, 1), -RETURN_DELTA_LIMIT, RETURN_DELTA_LIMIT));
}

void GrayPreset::cycleReturnDelta(const Toward toward) {
  int8_t& delta = returnDelta[routeIndex(toward)];
  delta = delta >= RETURN_DELTA_LIMIT ? static_cast<int8_t>(-RETURN_DELTA_LIMIT) : static_cast<int8_t>(delta + 1);
}

void GrayPreset::stepReversePercent(const Toward toward, const int direction) {
  uint8_t& percent = reversePercent[routeIndex(toward)];
  percent =
      static_cast<uint8_t>(clampedStep(percent, signedStep(direction, REVERSE_PERCENT_STEP), 0, REVERSE_PERCENT_MAX));
}

void GrayPreset::cycleReversePercent(const Toward toward) {
  uint8_t& percent = reversePercent[routeIndex(toward)];
  const int next = percent + REVERSE_PERCENT_STEP;
  percent = next > REVERSE_PERCENT_MAX ? 0 : static_cast<uint8_t>(next);
}

void GrayPreset::stepRate(const int delta, const uint8_t rateCount) {
  if (rateCount == 0) {
    rateIndex = 0;
    return;
  }
  rateIndex = static_cast<uint8_t>(clampedStep(rateIndex, delta, 0, rateCount - 1));
}

void GrayPreset::cycleRate(const uint8_t rateCount) {
  rateIndex = rateCount == 0 || rateIndex + 1 >= rateCount ? 0 : static_cast<uint8_t>(rateIndex + 1);
}

size_t encodeGrayPreset(const GrayPreset& preset, const PresetBase base, char* out, const size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  Writer w{out, capacity};
  w.text("{\"version\":");
  w.number(FILE_VERSION);
  w.text(",\"base\":\"");
  w.text(baseKeyOf(base));
  w.text("\",\"doses\":[");
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.number(preset.doseAt(i));
  }
  w.text("],\"hold\":[");
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.boolean(preset.holdAt(i));
  }
  w.text("],\"via\":[");
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) w.text(",");
    const uint8_t v = preset.viaAt(i);
    w.number(v < SHADE_COUNT ? v : FILE_VIA_NONE);
  }
  w.text("],\"retarget_dark_percent\":");
  w.number(preset.retargetDarkPercent);
  w.text(",\"retarget_light_percent\":");
  w.number(preset.retargetLightPercent);
  w.text(",\"rate\":");
  w.number(preset.rateIndex);
  w.text(",\"swap_polarity\":");
  w.boolean(preset.swapPolarity);
  w.text(",\"history\":");
  w.number(preset.historyMode);
  w.text(",\"return_delta\":[");
  for (size_t i = 0; i < ROUTE_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.number(preset.returnDelta[i]);
  }
  w.text("],\"reverse_percent\":[");
  for (size_t i = 0; i < ROUTE_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.number(preset.reversePercent[i]);
  }
  w.text("],\"weak_white_to_light\":");
  w.boolean(preset.weakWhiteToLight);
  w.text("}");
  if (w.failed) {
    out[0] = '\0';
    return 0;
  }
  return w.length;
}

GrayPresetLoad loadGrayPreset(const char* text, const size_t length, const GrayPreset& defaults, GrayPreset& out,
                              GrayPresetLoadInfo* info) {
  out = defaults;
  if (info) *info = GrayPresetLoadInfo{};
  if (text == nullptr) return GrayPresetLoad::Missing;
  // The values are read into `out` itself (no second preset on the stack: the 256-byte rule), which goes back to
  // `defaults` whenever the text turns out malformed.
  const auto malformed = [&out, &defaults] {
    out = defaults;
    return GrayPresetLoad::Malformed;
  };
  Reader reader(text, length);
  ReadState state;
  if (!reader.take('{')) return malformed();
  if (!reader.take('}')) {
    do {
      // The longest known key, "retarget_light_percent", is 22 characters.
      char key[24];
      bool overlong = false;
      if (!reader.string(key, sizeof(key), overlong) || !reader.take(':')) return malformed();
      // An overlong key is no known key: its value is skipped.
      const bool ok = overlong ? reader.skipValue(1) : readValue(reader, key, out, state);
      if (!ok) return malformed();
    } while (reader.take(','));
    if (!reader.take('}')) return malformed();
  }
  if (!reader.atEnd() || state.version == 0 || !arraysFitVersion(state)) return malformed();
  const bool version1 = state.version == FILE_VERSION_1;
  const uint8_t dropped = version1 ? expandVersion1(state, out) : 0;
  // The older single retarget key fills each direction the text did not set itself (its range was checked).
  const bool legacy = state.legacyRetarget >= 0;
  const bool legacyDark = legacy && !state.sawDark;
  const bool legacyLight = legacy && !state.sawLight;
  if (legacyDark) out.retargetDarkPercent = static_cast<uint8_t>(state.legacyRetarget);
  if (legacyLight) out.retargetLightPercent = static_cast<uint8_t>(state.legacyRetarget);
  if (!out.valid()) return malformed();
  if (info) {
    info->legacyDark = legacyDark;
    info->legacyLight = legacyLight;
    info->base = state.base;
    info->unknownBase = state.unknownBase;
    info->expandedVersion1 = version1;
    info->droppedVersion1Routes = dropped;
  }
  return GrayPresetLoad::Loaded;
}

}  // namespace GameDisplayLab

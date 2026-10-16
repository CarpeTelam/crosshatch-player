#include "GrayPreset.h"

#include <cstdio>
#include <cstring>

namespace GameDisplayLab {

namespace {

// InkBoy's default dose table (presets/grayscale/default.json, the digest's table), dose[from][to].
constexpr uint8_t INKBOY_DOSE[SHADE_COUNT][SHADE_COUNT] = {
    {0, 2, 4, 7},  // K > D, L, W
    {4, 0, 1, 6},  // D > K, L, W
    {4, 1, 0, 5},  // L > K, D, W
    {7, 3, 2, 0},  // W > K, D, L
};
// InkBoy's HOLD mask (gray_config.h:48-49), bit source * 4 + target.
constexpr uint16_t INKBOY_HOLD = 0xc667;
constexpr uint8_t INKBOY_RETARGET = 100;
// Routes LKL, DKD, DWD, LWL, DLD, LDL: L>K>L returns one dose short and reverses at 150 %; the rest are neutral.
constexpr int8_t INKBOY_RETURN_DELTA[ROUTE_COUNT] = {-1, 0, 0, 0, 0, 0};
constexpr uint8_t INKBOY_REVERSE_PERCENT[ROUTE_COUNT] = {150, 0, 0, 0, 0, 0};

constexpr int FILE_VERSION = 1;
constexpr uint8_t RATE_MAX = 255;
constexpr uint8_t HISTORY_MAX = HISTORY_RETURN | HISTORY_REVERSE;
// Nesting an unknown value may have before the reader calls the text malformed (bounds the recursion).
constexpr int MAX_DEPTH = 16;

uint8_t clampDose(const int value) {
  if (value < DOSE_MIN) return DOSE_MIN;
  if (value > DOSE_MAX) return DOSE_MAX;
  return static_cast<uint8_t>(value);
}

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

// `[a, b, ...]` of exactly `count` integers in low..high.
bool intArray(Reader& reader, int* out, const size_t count, const int low, const int high) {
  if (!reader.take('[')) return false;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0 && !reader.take(',')) return false;
    if (!intInRange(reader, low, high, out[i])) return false;
  }
  return reader.take(']');
}

// `[true, false, ...]` of exactly `count` booleans.
bool boolArray(Reader& reader, bool* out, const size_t count) {
  if (!reader.take('[')) return false;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0 && !reader.take(',')) return false;
    if (!reader.boolean(out[i])) return false;
  }
  return reader.take(']');
}

// One known key's value into `preset`; an unknown key's value is skipped. False when the value is malformed.
bool readValue(Reader& reader, const char* key, GrayPreset& preset, bool& sawVersion) {
  int number = 0;
  if (strcmp(key, "version") == 0) {
    sawVersion = true;
    return reader.integer(number) && number == FILE_VERSION;
  }
  if (strcmp(key, "doses") == 0) {
    int doses[TRANSITION_COUNT];
    if (!intArray(reader, doses, TRANSITION_COUNT, DOSE_MIN, DOSE_MAX)) return false;
    for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
      preset.dose[transitionFrom(i)][transitionTo(i)] = static_cast<uint8_t>(doses[i]);
    }
    return true;
  }
  if (strcmp(key, "hold") == 0) {
    bool hold[TRANSITION_COUNT];
    if (!boolArray(reader, hold, TRANSITION_COUNT)) return false;
    preset.holdMask = HOLD_DIAGONAL;
    for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) preset.setHold(i, hold[i]);
    return true;
  }
  if (strcmp(key, "retarget_percent") == 0) {
    if (!intInRange(reader, RETARGET_MIN, RETARGET_MAX, number)) return false;
    preset.retargetPercent = static_cast<uint8_t>(number);
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
    int deltas[ROUTE_COUNT];
    if (!intArray(reader, deltas, ROUTE_COUNT, -RETURN_DELTA_LIMIT, RETURN_DELTA_LIMIT)) return false;
    for (size_t i = 0; i < ROUTE_COUNT; ++i) preset.returnDelta[i] = static_cast<int8_t>(deltas[i]);
    return true;
  }
  if (strcmp(key, "reverse_percent") == 0) {
    int percents[ROUTE_COUNT];
    if (!intArray(reader, percents, ROUTE_COUNT, 0, REVERSE_PERCENT_MAX)) return false;
    for (size_t i = 0; i < ROUTE_COUNT; ++i) preset.reversePercent[i] = static_cast<uint8_t>(percents[i]);
    return true;
  }
  if (strcmp(key, "weak_white_to_light") == 0) return reader.boolean(preset.weakWhiteToLight);
  return reader.skipValue(1);
}

}  // namespace

GrayPreset GrayPreset::inkBoyDefaults() {
  GrayPreset preset{};
  memcpy(preset.dose, INKBOY_DOSE, sizeof(preset.dose));
  preset.holdMask = INKBOY_HOLD;
  preset.retargetPercent = INKBOY_RETARGET;
  preset.rateIndex = 0;
  preset.swapPolarity = false;
  preset.historyMode = 0;
  memcpy(preset.returnDelta, INKBOY_RETURN_DELTA, sizeof(preset.returnDelta));
  memcpy(preset.reversePercent, INKBOY_REVERSE_PERCENT, sizeof(preset.reversePercent));
  preset.weakWhiteToLight = false;
  return preset;
}

bool GrayPreset::valid() const {
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      const uint8_t d = dose[from][to];
      if (from == to ? d != 0 : (d < DOSE_MIN || d > DOSE_MAX)) return false;
    }
  }
  if ((holdMask & HOLD_DIAGONAL) != HOLD_DIAGONAL) return false;
  if (retargetPercent < RETARGET_MIN || retargetPercent > RETARGET_MAX) return false;
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

void GrayPreset::stepDose(const uint8_t index, const int delta) {
  uint8_t& d = dose[transitionFrom(index)][transitionTo(index)];
  d = clampDose(d + delta);
}

void GrayPreset::cycleDose(const uint8_t index) {
  uint8_t& d = dose[transitionFrom(index)][transitionTo(index)];
  d = d >= DOSE_MAX ? DOSE_MIN : static_cast<uint8_t>(d + 1);
}

void GrayPreset::setHold(const uint8_t index, const bool hold) {
  const uint16_t bit = holdBit(transitionFrom(index), transitionTo(index));
  holdMask = static_cast<uint16_t>(hold ? holdMask | bit : holdMask & ~bit);
}

void GrayPreset::toggleHold(const uint8_t index) { setHold(index, !holdAt(index)); }

void GrayPreset::stepRetarget(const int direction) {
  const int step = direction > 0 ? RETARGET_STEP : (direction < 0 ? -RETARGET_STEP : 0);
  const int next = retargetPercent + step;
  if (next < RETARGET_MIN) {
    retargetPercent = RETARGET_MIN;
  } else if (next > RETARGET_MAX) {
    retargetPercent = RETARGET_MAX;
  } else {
    retargetPercent = static_cast<uint8_t>(next);
  }
}

void GrayPreset::cycleRetarget() {
  const int next = retargetPercent + RETARGET_STEP;
  retargetPercent = next > RETARGET_MAX ? RETARGET_MIN : static_cast<uint8_t>(next);
}

void GrayPreset::stepRate(const int delta, const uint8_t rateCount) {
  if (rateCount == 0) {
    rateIndex = 0;
    return;
  }
  const int next = static_cast<int>(rateIndex) + delta;
  if (next < 0) {
    rateIndex = 0;
  } else if (next >= rateCount) {
    rateIndex = static_cast<uint8_t>(rateCount - 1);
  } else {
    rateIndex = static_cast<uint8_t>(next);
  }
}

void GrayPreset::cycleRate(const uint8_t rateCount) {
  rateIndex = rateCount == 0 || rateIndex + 1 >= rateCount ? 0 : static_cast<uint8_t>(rateIndex + 1);
}

size_t encodeGrayPreset(const GrayPreset& preset, char* out, const size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  Writer w{out, capacity};
  w.text("{\"version\":");
  w.number(FILE_VERSION);
  w.text(",\"doses\":[");
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.number(preset.doseAt(i));
  }
  w.text("],\"hold\":[");
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) w.text(",");
    w.boolean(preset.holdAt(i));
  }
  w.text("],\"retarget_percent\":");
  w.number(preset.retargetPercent);
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

GrayPresetLoad loadGrayPreset(const char* text, const size_t length, GrayPreset& out) {
  out = GrayPreset::inkBoyDefaults();
  if (text == nullptr) return GrayPresetLoad::Missing;
  GrayPreset read = GrayPreset::inkBoyDefaults();
  Reader reader(text, length);
  bool sawVersion = false;
  if (!reader.take('{')) return GrayPresetLoad::Malformed;
  if (!reader.take('}')) {
    do {
      char key[24];
      bool overlong = false;
      if (!reader.string(key, sizeof(key), overlong) || !reader.take(':')) return GrayPresetLoad::Malformed;
      // An overlong key is no known key: its value is skipped.
      const bool ok = overlong ? reader.skipValue(1) : readValue(reader, key, read, sawVersion);
      if (!ok) return GrayPresetLoad::Malformed;
    } while (reader.take(','));
    if (!reader.take('}')) return GrayPresetLoad::Malformed;
  }
  if (!reader.atEnd() || !sawVersion || !read.valid()) return GrayPresetLoad::Malformed;
  out = read;
  return GrayPresetLoad::Loaded;
}

}  // namespace GameDisplayLab

// The gray engine's preset (lib/GameDisplayLab/GrayPreset): InkBoy's defaults against the D2 round 2 digest, the
// transition order, the tuning steps, and the std-only JSON codec of /.crosshatch/display-lab.json (the plan's Preset
// load and Tuning edit matrix rows).

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GrayPreset.h"

namespace {

using namespace GameDisplayLab;

constexpr const char* DEFAULT_TEXT =
    "{\"version\":1,\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2],\"hold\":[true,true,false,false,true,false,false,true,false,"
    "false,false,true],\"retarget_percent\":100,\"rate\":0,\"swap_polarity\":false,\"history\":0,\"return_delta\":[-1,"
    "0,0,0,0,0],\"reverse_percent\":[150,0,0,0,0,0],\"weak_white_to_light\":false}";

GrayPresetLoad load(const std::string& text, GrayPreset& out) { return loadGrayPreset(text.data(), text.size(), out); }

bool samePreset(const GrayPreset& a, const GrayPreset& b) {
  return memcmp(a.dose, b.dose, sizeof(a.dose)) == 0 && a.holdMask == b.holdMask &&
         a.retargetPercent == b.retargetPercent && a.rateIndex == b.rateIndex && a.swapPolarity == b.swapPolarity &&
         a.historyMode == b.historyMode && memcmp(a.returnDelta, b.returnDelta, sizeof(a.returnDelta)) == 0 &&
         memcmp(a.reversePercent, b.reversePercent, sizeof(a.reversePercent)) == 0 &&
         a.weakWhiteToLight == b.weakWhiteToLight;
}

// A preset that differs from the defaults in every field.
GrayPreset changedPreset() {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    p.dose[transitionFrom(i)][transitionTo(i)] = static_cast<uint8_t>(12 - i);
    p.setHold(i, i % 2 == 0);
  }
  p.retargetPercent = 75;
  p.rateIndex = 2;
  p.swapPolarity = true;
  p.historyMode = 3;
  const int8_t deltas[ROUTE_COUNT] = {-11, 11, 0, 3, -2, 1};
  const uint8_t percents[ROUTE_COUNT] = {0, 250, 100, 50, 125, 175};
  memcpy(p.returnDelta, deltas, sizeof(deltas));
  memcpy(p.reversePercent, percents, sizeof(percents));
  p.weakWhiteToLight = true;
  return p;
}

TEST(GrayPreset, DefaultsAreInkBoysDoseTable) {
  // The digest's table, From > To: dose.
  struct Row {
    Shade from;
    Shade to;
    uint8_t dose;
    bool hold;
  };
  const Row rows[] = {
      {Shade::K, Shade::D, 2, true},  {Shade::K, Shade::L, 4, true},  {Shade::K, Shade::W, 7, false},
      {Shade::D, Shade::K, 4, false}, {Shade::D, Shade::L, 1, true},  {Shade::D, Shade::W, 6, false},
      {Shade::L, Shade::K, 4, false}, {Shade::L, Shade::D, 1, true},  {Shade::L, Shade::W, 5, false},
      {Shade::W, Shade::K, 7, false}, {Shade::W, Shade::D, 3, false}, {Shade::W, Shade::L, 2, true},
  };
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    const uint8_t from = static_cast<uint8_t>(rows[i].from);
    const uint8_t to = static_cast<uint8_t>(rows[i].to);
    EXPECT_EQ(transitionFrom(i), from) << "transition " << int(i);
    EXPECT_EQ(transitionTo(i), to) << "transition " << int(i);
    EXPECT_EQ(transitionIndex(from, to), i);
    EXPECT_EQ(p.dose[from][to], rows[i].dose) << "transition " << int(i);
    EXPECT_EQ(p.doseAt(i), rows[i].dose);
    EXPECT_EQ(p.holdAt(i), rows[i].hold) << "transition " << int(i);
  }
  for (uint8_t s = 0; s < SHADE_COUNT; ++s) EXPECT_EQ(p.dose[s][s], 0);
}

TEST(GrayPreset, DefaultsAreInkBoysOtherValues) {
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  EXPECT_EQ(p.holdMask, 0xc667);
  EXPECT_EQ(p.holdMask & HOLD_DIAGONAL, HOLD_DIAGONAL);
  EXPECT_EQ(p.retargetPercent, 100);
  EXPECT_EQ(p.rateIndex, 0);
  EXPECT_FALSE(p.swapPolarity);
  EXPECT_EQ(p.historyMode, 0);  // history implemented, off by default
  const int8_t deltas[ROUTE_COUNT] = {-1, 0, 0, 0, 0, 0};
  const uint8_t percents[ROUTE_COUNT] = {150, 0, 0, 0, 0, 0};
  for (size_t r = 0; r < ROUTE_COUNT; ++r) {
    EXPECT_EQ(p.returnDelta[r], deltas[r]) << "route " << r;
    EXPECT_EQ(p.reversePercent[r], percents[r]) << "route " << r;
  }
  EXPECT_FALSE(p.weakWhiteToLight);
  EXPECT_TRUE(p.valid());
}

TEST(GrayPreset, ValidRejectsEveryOutOfRangeField) {
  const auto broken = [](auto change) {
    GrayPreset p = GrayPreset::inkBoyDefaults();
    change(p);
    return !p.valid();
  };
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 13; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[2][2] = 1; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.holdMask = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetPercent = 49; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetPercent = 151; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.historyMode = 4; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.returnDelta[3] = 12; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.returnDelta[3] = -12; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.reversePercent[5] = 251; }));
}

TEST(GrayPresetTuning, DoseStepsClampAndTapCycles) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t kw = transitionIndex(0, 3);  // K > W, 7
  p.stepDose(kw, +1);
  EXPECT_EQ(p.doseAt(kw), 8);
  for (int i = 0; i < 10; ++i) p.stepDose(kw, +1);
  EXPECT_EQ(p.doseAt(kw), 12);  // max 12
  p.cycleDose(kw);
  EXPECT_EQ(p.doseAt(kw), 1);  // a tap wraps 12 to 1
  p.stepDose(kw, -1);
  EXPECT_EQ(p.doseAt(kw), 1);  // min 1
  p.cycleDose(kw);
  EXPECT_EQ(p.doseAt(kw), 2);
  EXPECT_TRUE(p.valid());
}

TEST(GrayPresetTuning, HoldSetsAndToggles) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t dk = transitionIndex(1, 0);  // D > K, hold off
  p.setHold(dk, true);
  EXPECT_TRUE(p.holdAt(dk));
  EXPECT_EQ(p.holdMask, 0xc667 | holdBit(1, 0));
  p.toggleHold(dk);
  EXPECT_FALSE(p.holdAt(dk));
  EXPECT_EQ(p.holdMask, 0xc667);
  p.setHold(dk, false);
  EXPECT_EQ(p.holdMask, 0xc667);
}

TEST(GrayPresetTuning, RetargetStepsBy25WithinItsRange) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.stepRetarget(+1);
  EXPECT_EQ(p.retargetPercent, 125);
  p.stepRetarget(+1);
  p.stepRetarget(+1);
  EXPECT_EQ(p.retargetPercent, 150);
  p.cycleRetarget();
  EXPECT_EQ(p.retargetPercent, 50);
  p.stepRetarget(-1);
  EXPECT_EQ(p.retargetPercent, 50);
  p.cycleRetarget();
  EXPECT_EQ(p.retargetPercent, 75);
  p.stepRetarget(0);
  EXPECT_EQ(p.retargetPercent, 75);
}

TEST(GrayPresetTuning, RateStaysBelowTheBackendsCount) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.stepRate(-1, 3);
  EXPECT_EQ(p.rateIndex, 0);
  p.stepRate(+1, 3);
  p.stepRate(+1, 3);
  p.stepRate(+1, 3);
  EXPECT_EQ(p.rateIndex, 2);
  p.cycleRate(3);
  EXPECT_EQ(p.rateIndex, 0);
  p.cycleRate(3);
  EXPECT_EQ(p.rateIndex, 1);
  p.stepRate(+1, 0);
  EXPECT_EQ(p.rateIndex, 0);
  p.toggleSwap();
  EXPECT_TRUE(p.swapPolarity);
  p.toggleSwap();
  EXPECT_FALSE(p.swapPolarity);
}

TEST(GrayPresetCodec, DefaultsEncodeToTheDocumentedText) {
  char buffer[512];
  const size_t length = encodeGrayPreset(GrayPreset::inkBoyDefaults(), buffer, sizeof(buffer));
  EXPECT_EQ(std::string(buffer), DEFAULT_TEXT);
  EXPECT_EQ(length, strlen(DEFAULT_TEXT));
}

TEST(GrayPresetCodec, RoundTripKeepsEveryField) {
  const GrayPreset written = changedPreset();
  ASSERT_TRUE(written.valid());
  char buffer[512];
  const size_t length = encodeGrayPreset(written, buffer, sizeof(buffer));
  ASSERT_GT(length, 0u);
  GrayPreset read{};
  EXPECT_EQ(loadGrayPreset(buffer, length, read), GrayPresetLoad::Loaded);
  EXPECT_TRUE(samePreset(read, written));
}

TEST(GrayPresetCodec, TooSmallBufferWritesNothing) {
  const size_t needed = strlen(DEFAULT_TEXT) + 1;
  char buffer[512];
  memset(buffer, 'x', sizeof(buffer));
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), buffer, needed - 1), 0u);
  EXPECT_EQ(buffer[0], '\0');
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), buffer, needed), needed - 1);
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), nullptr, 100), 0u);
}

TEST(GrayPresetCodec, MissingFileGivesDefaults) {
  GrayPreset out = changedPreset();
  EXPECT_EQ(loadGrayPreset(nullptr, 0, out), GrayPresetLoad::Missing);
  EXPECT_TRUE(samePreset(out, GrayPreset::inkBoyDefaults()));
}

TEST(GrayPresetCodec, AbsentKeysKeepDefaultsAndWhitespaceIsAllowed) {
  GrayPreset out{};
  EXPECT_EQ(load(" {\n  \"version\" : 1 ,\n  \"retarget_percent\": 125\n}\n", out), GrayPresetLoad::Loaded);
  GrayPreset expected = GrayPreset::inkBoyDefaults();
  expected.retargetPercent = 125;
  EXPECT_TRUE(samePreset(out, expected));
}

TEST(GrayPresetCodec, UnknownKeysAndNestedValuesAreSkipped) {
  GrayPreset out{};
  const std::string text =
      "{\"comment\":\"panel \\\"B\\\" batch\",\"version\":1,\"extra\":{\"a\":[1,{\"b\":null}],\"c\":-2.5e3},"
      "\"rate\":1,\"list\":[true,false,\"x\"],\"a_very_long_unknown_key_name_indeed\":7}";
  EXPECT_EQ(load(text, out), GrayPresetLoad::Loaded);
  GrayPreset expected = GrayPreset::inkBoyDefaults();
  expected.rateIndex = 1;
  EXPECT_TRUE(samePreset(out, expected));
}

TEST(GrayPresetCodec, MalformedTextGivesDefaults) {
  const char* cases[] = {
      // parse errors
      "",
      "{",
      "[1]",
      "{\"version\":1,}",
      "{\"version\":1} trailing",
      "{\"version\" 1}",
      "{\"version\":1,\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2}",
      "{\"version\":1,\"extra\":[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]}",
      // version
      "{}",
      "{\"rate\":1}",
      "{\"version\":2}",
      "{\"version\":\"1\"}",
      // range
      "{\"version\":1,\"doses\":[0,4,7,4,1,6,4,1,5,7,3,2]}",
      "{\"version\":1,\"doses\":[2,4,7,4,1,6,4,1,5,7,3,13]}",
      "{\"version\":1,\"retarget_percent\":49}",
      "{\"version\":1,\"retarget_percent\":151}",
      "{\"version\":1,\"rate\":256}",
      "{\"version\":1,\"rate\":-1}",
      "{\"version\":1,\"history\":4}",
      "{\"version\":1,\"return_delta\":[-12,0,0,0,0,0]}",
      "{\"version\":1,\"reverse_percent\":[251,0,0,0,0,0]}",
      "{\"version\":1,\"rate\":99999999999999999999}",
      // type and length
      "{\"version\":1,\"doses\":[2,4,7,4,1,6,4,1,5,7,3]}",
      "{\"version\":1,\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2,2]}",
      "{\"version\":1,\"doses\":7}",
      "{\"version\":1,\"hold\":[1,1,0,0,1,0,0,1,0,0,0,1]}",
      "{\"version\":1,\"swap_polarity\":1}",
      "{\"version\":1,\"weak_white_to_light\":\"yes\"}",
      "{\"version\":1,\"retarget_percent\":100.0}",
      "{\"version\":1,\"rate\":null}",
  };
  for (const char* text : cases) {
    GrayPreset out = changedPreset();
    EXPECT_EQ(load(text, out), GrayPresetLoad::Malformed) << text;
    EXPECT_TRUE(samePreset(out, GrayPreset::inkBoyDefaults())) << text;
  }
}

TEST(GrayPresetCodec, HoldArrayKeepsTheDiagonalBits) {
  GrayPreset out{};
  EXPECT_EQ(
      load("{\"version\":1,\"hold\":[false,false,false,false,false,false,false,false,false,false,false,false]}", out),
      GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_DIAGONAL);
}

}  // namespace

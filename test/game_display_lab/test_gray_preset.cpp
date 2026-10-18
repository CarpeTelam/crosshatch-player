// The gray engine's preset (lib/GameDisplayLab/GrayPreset): InkBoy's defaults against the D2 round 2 digest, the
// UltraChip defaults against the owner's table (D12), the transition order, the tuning steps (doses to 24, the
// retarget split), and the std-only JSON codec of /.crosshatch/display-lab.json with its older `retarget_percent`
// key (the plans' Preset load, Tuning edit, Retarget split, Old preset file, Dose 24 and Defaults matrix rows).

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GrayPreset.h"

namespace {

using namespace GameDisplayLab;

constexpr const char* DEFAULT_TEXT =
    "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2],\"hold\":[true,true,false,false,true,false,"
    "false,true,false,"
    "false,false,true],\"retarget_dark_percent\":100,\"retarget_light_percent\":100,\"rate\":0,\"swap_polarity\":"
    "false,\"history\":0,\"return_delta\":[-1,0,0,0,0,0],\"reverse_percent\":[150,0,0,0,0,0],\"weak_white_to_light\":"
    "false}";

GrayPresetLoad load(const std::string& text, GrayPreset& out, GrayPresetLoadInfo* info = nullptr) {
  return loadGrayPreset(text.data(), text.size(), GrayPreset::inkBoyDefaults(), out, info);
}

bool samePreset(const GrayPreset& a, const GrayPreset& b) {
  return memcmp(a.dose, b.dose, sizeof(a.dose)) == 0 && a.holdMask == b.holdMask &&
         a.retargetDarkPercent == b.retargetDarkPercent && a.retargetLightPercent == b.retargetLightPercent &&
         a.rateIndex == b.rateIndex && a.swapPolarity == b.swapPolarity && a.historyMode == b.historyMode &&
         memcmp(a.returnDelta, b.returnDelta, sizeof(a.returnDelta)) == 0 &&
         memcmp(a.reversePercent, b.reversePercent, sizeof(a.reversePercent)) == 0 &&
         a.weakWhiteToLight == b.weakWhiteToLight;
}

// A preset that differs from the defaults in every field.
GrayPreset changedPreset() {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    p.dose[transitionFrom(i)][transitionTo(i)] = static_cast<uint8_t>(24 - 2 * i);  // 24 down to 2
    p.setHold(i, i % 2 == 0);
  }
  p.retargetDarkPercent = 75;
  p.retargetLightPercent = 125;
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
  EXPECT_EQ(p.retargetDarkPercent, 100);
  EXPECT_EQ(p.retargetLightPercent, 100);
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

TEST(GrayPreset, UltraChipDefaultsAreTheOwnersTableWithInkBoysRest) {
  // D12, calibrated: [2,7,12,7,2,10,7,6,9,12,5,3] in the order K>D, K>L, K>W, D>K, D>L, D>W, L>K, L>D, L>W, W>K, W>D,
  // W>L.
  const uint8_t table[TRANSITION_COUNT] = {2, 7, 12, 7, 2, 10, 7, 6, 9, 12, 5, 3};
  const GrayPreset u = GrayPreset::ultraChipDefaults();
  const GrayPreset ink = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    EXPECT_EQ(u.doseAt(i), table[i]) << "transition " << int(i);
    EXPECT_EQ(u.holdAt(i), ink.holdAt(i)) << "transition " << int(i);
  }
  for (uint8_t s = 0; s < SHADE_COUNT; ++s) EXPECT_EQ(u.dose[s][s], 0);
  GrayPreset rest = u;
  memcpy(rest.dose, ink.dose, sizeof(rest.dose));
  EXPECT_TRUE(samePreset(rest, ink));  // everything but the doses is InkBoy's
  EXPECT_TRUE(u.valid());
  EXPECT_TRUE(samePreset(presetDefaults(PresetBase::UltraChip), u));
  EXPECT_TRUE(samePreset(presetDefaults(PresetBase::InkBoy), ink));
  EXPECT_STREQ(presetBaseName(PresetBase::UltraChip), "UltraChip");
  EXPECT_STREQ(presetBaseName(PresetBase::InkBoy), "InkBoy's");
}

TEST(GrayPreset, LongestRunSlotsCountsOneDoseASlotForHeldTransitions) {
  // InkBoy's: K>L 4 held and W>K 7 not held (ceil(7 / 2)) both take 4 slots.
  EXPECT_EQ(GrayPreset::inkBoyDefaults().longestRunSlots(), 4);
  // UltraChip: K>L 7 held, ahead of L>D 6 held and K>W and W>K 12 not held (6).
  EXPECT_EQ(GrayPreset::ultraChipDefaults().longestRunSlots(), 7);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t wk = transitionIndex(3, 0);  // W > K, hold off
  p.dose[3][0] = DOSE_MAX;
  EXPECT_EQ(p.longestRunSlots(), 12);  // 24 doses, two a slot
  p.setHold(wk, true);
  EXPECT_EQ(p.longestRunSlots(), 24);  // held: one a slot
  p.dose[3][0] = 9;
  p.setHold(wk, false);
  EXPECT_EQ(p.longestRunSlots(), 5);  // odd: ceil(9 / 2)
}

TEST(GrayPreset, ValidRejectsEveryOutOfRangeField) {
  const auto broken = [](auto change) {
    GrayPreset p = GrayPreset::inkBoyDefaults();
    change(p);
    return !p.valid();
  };
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 25; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[2][2] = 1; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.holdMask = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetDarkPercent = 49; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetDarkPercent = 151; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetLightPercent = 49; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetLightPercent = 151; }));
  EXPECT_FALSE(broken([](GrayPreset& p) { p.dose[0][1] = 24; }));
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
  EXPECT_EQ(p.doseAt(kw), 18);  // past the old cap of 12
  for (int i = 0; i < 10; ++i) p.stepDose(kw, +1);
  EXPECT_EQ(p.doseAt(kw), 24);  // max 24
  p.cycleDose(kw);
  EXPECT_EQ(p.doseAt(kw), 1);  // a tap wraps 24 to 1
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

TEST(GrayPresetTuning, RetargetStepsBy25WithinItsRangeOneDirectionAtATime) {
  for (const Toward toward : {Toward::Dark, Toward::Light}) {
    const Toward other = toward == Toward::Dark ? Toward::Light : Toward::Dark;
    GrayPreset p = GrayPreset::inkBoyDefaults();
    p.stepRetarget(toward, +1);
    EXPECT_EQ(p.retargetPercent(toward), 125);
    p.stepRetarget(toward, +1);
    p.stepRetarget(toward, +1);
    EXPECT_EQ(p.retargetPercent(toward), 150);
    p.cycleRetarget(toward);
    EXPECT_EQ(p.retargetPercent(toward), 50);
    p.stepRetarget(toward, -1);
    EXPECT_EQ(p.retargetPercent(toward), 50);
    p.cycleRetarget(toward);
    EXPECT_EQ(p.retargetPercent(toward), 75);
    p.stepRetarget(toward, 0);
    EXPECT_EQ(p.retargetPercent(toward), 75);
    EXPECT_EQ(p.retargetPercent(other), 100);  // the other direction never moves
  }
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.retargetDarkPercent = 125;
  p.retargetLightPercent = 75;
  EXPECT_EQ(p.retargetPercent(Toward::Dark), 125);
  EXPECT_EQ(p.retargetPercent(Toward::Light), 75);
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
  const size_t length = encodeGrayPreset(GrayPreset::inkBoyDefaults(), PresetBase::InkBoy, buffer, sizeof(buffer));
  EXPECT_EQ(std::string(buffer), DEFAULT_TEXT);
  EXPECT_EQ(length, strlen(DEFAULT_TEXT));
}

TEST(GrayPresetCodec, RoundTripKeepsEveryField) {
  const GrayPreset written = changedPreset();
  ASSERT_TRUE(written.valid());
  char buffer[512];
  const size_t length = encodeGrayPreset(written, PresetBase::UltraChip, buffer, sizeof(buffer));
  ASSERT_GT(length, 0u);
  GrayPreset read{};
  GrayPresetLoadInfo info;
  info.legacyDark = true;
  EXPECT_EQ(loadGrayPreset(buffer, length, GrayPreset::inkBoyDefaults(), read, &info), GrayPresetLoad::Loaded);
  EXPECT_TRUE(samePreset(read, written));
  EXPECT_FALSE(info.legacyDark);
  EXPECT_FALSE(info.legacyLight);
  EXPECT_EQ(info.base, PresetBase::UltraChip);                                     // the base it was saved with
  EXPECT_EQ(std::string(buffer).find("\"retarget_percent\""), std::string::npos);  // only the two new keys
}

TEST(GrayPresetCodec, TooSmallBufferWritesNothing) {
  const size_t needed = strlen(DEFAULT_TEXT) + 1;
  char buffer[512];
  memset(buffer, 'x', sizeof(buffer));
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), PresetBase::InkBoy, buffer, needed - 1), 0u);
  EXPECT_EQ(buffer[0], '\0');
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), PresetBase::InkBoy, buffer, needed), needed - 1);
  EXPECT_EQ(encodeGrayPreset(GrayPreset::inkBoyDefaults(), PresetBase::InkBoy, nullptr, 100), 0u);
}

TEST(GrayPresetCodec, MissingFileGivesTheGivenDefaults) {
  for (const PresetBase base : {PresetBase::InkBoy, PresetBase::UltraChip}) {
    GrayPreset out = changedPreset();
    EXPECT_EQ(loadGrayPreset(nullptr, 0, presetDefaults(base), out), GrayPresetLoad::Missing);
    EXPECT_TRUE(samePreset(out, presetDefaults(base)));
  }
}

TEST(GrayPresetCodec, AbsentKeysKeepTheGivenDefaultsAndWhitespaceIsAllowed) {
  GrayPreset out{};
  EXPECT_EQ(load(" {\n  \"version\" : 1 ,\n  \"retarget_dark_percent\": 125\n}\n", out), GrayPresetLoad::Loaded);
  GrayPreset expected = GrayPreset::inkBoyDefaults();
  expected.retargetDarkPercent = 125;
  EXPECT_TRUE(samePreset(out, expected));
  // On an UltraChip panel the absent keys keep the UltraChip doses.
  const std::string text = "{\"version\":1,\"rate\":1}";
  EXPECT_EQ(loadGrayPreset(text.data(), text.size(), GrayPreset::ultraChipDefaults(), out), GrayPresetLoad::Loaded);
  expected = GrayPreset::ultraChipDefaults();
  expected.rateIndex = 1;
  EXPECT_TRUE(samePreset(out, expected));
}

TEST(GrayPresetCodec, OldRetargetKeyFillsTheDirectionsANewKeyDoesNotSet) {
  GrayPreset out{};
  GrayPresetLoadInfo info;
  // A dlab-c1 / dlab-c2 file: one value, read as both.
  EXPECT_EQ(load("{\"version\":1,\"retarget_percent\":150}", out, &info), GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.legacyDark);
  EXPECT_TRUE(info.legacyLight);
  EXPECT_EQ(out.retargetDarkPercent, 150);
  EXPECT_EQ(out.retargetLightPercent, 150);
  // A new key wins over the old one for its direction, before or after it in the text; the info names the one the
  // old key filled.
  EXPECT_EQ(load("{\"version\":1,\"retarget_light_percent\":75,\"retarget_percent\":125}", out, &info),
            GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.legacyDark);
  EXPECT_FALSE(info.legacyLight);
  EXPECT_EQ(out.retargetDarkPercent, 125);
  EXPECT_EQ(out.retargetLightPercent, 75);
  EXPECT_EQ(load("{\"version\":1,\"retarget_percent\":125,\"retarget_dark_percent\":50}", out, &info),
            GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.legacyDark);
  EXPECT_TRUE(info.legacyLight);
  EXPECT_EQ(out.retargetDarkPercent, 50);
  EXPECT_EQ(out.retargetLightPercent, 125);
  // Both new keys: the old one fills nothing.
  EXPECT_EQ(load("{\"version\":1,\"retarget_dark_percent\":75,\"retarget_percent\":125,"
                 "\"retarget_light_percent\":100}",
                 out, &info),
            GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.legacyDark);
  EXPECT_FALSE(info.legacyLight);
  EXPECT_EQ(out.retargetDarkPercent, 75);
  EXPECT_EQ(out.retargetLightPercent, 100);
  // Saved again, the file carries the two new keys.
  char buffer[512];
  ASSERT_GT(encodeGrayPreset(out, PresetBase::InkBoy, buffer, sizeof(buffer)), 0u);
  EXPECT_NE(std::string(buffer).find("\"retarget_dark_percent\":75,\"retarget_light_percent\":100"), std::string::npos);
  // A malformed file says nothing of the old key.
  info.legacyDark = true;
  EXPECT_EQ(load("{\"version\":1,\"retarget_percent\":125,\"rate\":-1}", out, &info), GrayPresetLoad::Malformed);
  EXPECT_FALSE(info.legacyDark);
  EXPECT_FALSE(info.legacyLight);
}

TEST(GrayPresetCodec, BaseKeyIsWrittenAndReadAndMayBeAbsent) {
  char buffer[512];
  ASSERT_GT(encodeGrayPreset(GrayPreset::ultraChipDefaults(), PresetBase::UltraChip, buffer, sizeof(buffer)), 0u);
  EXPECT_EQ(std::string(buffer).rfind("{\"version\":1,\"base\":\"ultrachip\",\"doses\":[2,7,12,", 0), 0u);
  GrayPreset out{};
  GrayPresetLoadInfo info;
  // Read back over the other controller's defaults: loaded all the same, and the info names the file's base (the
  // caller logs the mismatch).
  EXPECT_EQ(load(buffer, out, &info), GrayPresetLoad::Loaded);
  EXPECT_EQ(info.base, PresetBase::UltraChip);
  EXPECT_TRUE(samePreset(out, GrayPreset::ultraChipDefaults()));
  EXPECT_EQ(load("{\"version\":1,\"base\":\"inkboy\"}", out, &info), GrayPresetLoad::Loaded);
  EXPECT_EQ(info.base, PresetBase::InkBoy);
  // A file saved before the key existed has none.
  EXPECT_EQ(load("{\"version\":1,\"rate\":1}", out, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.base.has_value());
  // The key is informational: any other value, or another type, is ignored and reported, and the file still loads.
  for (const char* text :
       {"{\"version\":1,\"base\":\"ssd1677\",\"rate\":2}", "{\"version\":1,\"base\":\"inkboyx\",\"rate\":2}",
        "{\"version\":1,\"base\":\"a_very_long_base_name\",\"rate\":2}", "{\"version\":1,\"base\":1,\"rate\":2}",
        "{\"version\":1,\"base\":null,\"rate\":2}", "{\"version\":1,\"base\":[\"inkboy\"],\"rate\":2}"}) {
    out = GrayPreset::inkBoyDefaults();
    EXPECT_EQ(load(text, out, &info), GrayPresetLoad::Loaded) << text;
    EXPECT_FALSE(info.base.has_value()) << text;
    EXPECT_TRUE(info.unknownBase) << text;
    EXPECT_EQ(out.rateIndex, 2) << text;
  }
  // A known value clears the report, and a later unknown one replaces a known one.
  EXPECT_EQ(load("{\"version\":1,\"base\":\"inkboy\"}", out, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.unknownBase);
  EXPECT_EQ(load("{\"version\":1,\"base\":\"ultrachip\",\"base\":\"x\"}", out, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.base.has_value());
  EXPECT_TRUE(info.unknownBase);
  // A broken string is still a parse error.
  EXPECT_EQ(load("{\"version\":1,\"base\":\"inkboy}", out, &info), GrayPresetLoad::Malformed);
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
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2}",
      "{\"version\":1,\"extra\":[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]}",
      // version
      "{}",
      "{\"rate\":1}",
      "{\"version\":2}",
      "{\"version\":\"1\"}",
      // range
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":[0,4,7,4,1,6,4,1,5,7,3,2]}",
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3,25]}",
      "{\"version\":1,\"retarget_percent\":49}",
      "{\"version\":1,\"retarget_percent\":151}",
      "{\"version\":1,\"retarget_dark_percent\":49}",
      "{\"version\":1,\"retarget_dark_percent\":151}",
      "{\"version\":1,\"retarget_light_percent\":49}",
      "{\"version\":1,\"retarget_light_percent\":151}",
      "{\"version\":1,\"retarget_light_percent\":true}",
      "{\"version\":1,\"rate\":256}",
      "{\"version\":1,\"rate\":-1}",
      "{\"version\":1,\"history\":4}",
      "{\"version\":1,\"return_delta\":[-12,0,0,0,0,0]}",
      "{\"version\":1,\"reverse_percent\":[251,0,0,0,0,0]}",
      "{\"version\":1,\"rate\":99999999999999999999}",
      // type and length
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3]}",
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2,2]}",
      "{\"version\":1,\"base\":\"inkboy\",\"doses\":7}",
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

TEST(GrayPresetCodec, DosesUpTo24Load) {
  GrayPreset out{};
  EXPECT_EQ(load("{\"version\":1,\"doses\":[24,1,24,1,24,1,24,1,24,1,24,24]}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.doseAt(0), 24);
  EXPECT_EQ(out.doseAt(11), 24);
  EXPECT_EQ(out.doseAt(1), 1);
}

TEST(GrayPresetCodec, HoldArrayKeepsTheDiagonalBits) {
  GrayPreset out{};
  EXPECT_EQ(
      load("{\"version\":1,\"hold\":[false,false,false,false,false,false,false,false,false,false,false,false]}", out),
      GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_DIAGONAL);
}

}  // namespace

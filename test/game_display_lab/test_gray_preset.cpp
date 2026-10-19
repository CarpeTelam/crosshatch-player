// The gray engine's preset (lib/GameDisplayLab/GrayPreset) at seven levels (story dlab-g2, the epic's D14): the
// transition order and hold bits of the 7x7 table, InkBoy's defaults as the version 1 expansion of its table, the
// UltraChip defaults against D14's table and its via, the expansion rules, the tuning steps (doses to 24, the retarget
// split, vias, history, routes), and the std-only JSON codec of /.crosshatch/display-lab.json: version 2 round-trips,
// version 1 files expand, the older `retarget_percent` key (the plans' Preset load, Tuning edit, Retarget split, Old
// preset file, Dose 24, Defaults, Version 1 file and Version 2 file matrix rows).

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GrayPreset.h"

namespace {

using namespace GameDisplayLab;

// InkBoy's defaults as the file writes them: its four-shade table expanded to seven levels.
constexpr const char* DEFAULT_TEXT =
    "{\"version\":2,\"base\":\"inkboy\",\"doses\":[1,2,3,4,6,7,2,1,2,3,5,7,4,2,1,1,4,6,4,3,1,1,3,6,4,3,1,1,3,5,6,4,2,2,"
    "1,3,7,5,3,3,2,1],\"hold\":[true,true,true,true,true,false,false,true,true,true,true,false,false,false,true,true,"
    "true,false,false,true,true,true,true,false,false,true,true,true,false,false,false,true,true,true,true,false,false,"
    "false,false,true,true,true],\"via\":[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-"
    "1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],\"retarget_dark_percent\":100,\"retarget_light_percent\":100,"
    "\"rate\":0,\"swap_polarity\":false,\"history\":0,\"return_delta\":[0,-1],\"reverse_percent\":[0,150],\"weak_white_"
    "to_light\":false}";

// InkBoy's defaults as a version 1 file wrote them (dlab-c1 .. dlab-g1).
constexpr const char* V1_DEFAULT_TEXT =
    "{\"version\":1,\"base\":\"inkboy\",\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2],\"hold\":[true,true,false,false,true,false,"
    "false,true,false,false,false,true],\"retarget_dark_percent\":100,\"retarget_light_percent\":100,\"rate\":0,"
    "\"swap_polarity\":false,\"history\":0,\"return_delta\":[-1,0,0,0,0,0],\"reverse_percent\":[150,0,0,0,0,0],"
    "\"weak_white_to_light\":false}";

GrayPresetLoad load(const std::string& text, GrayPreset& out, GrayPresetLoadInfo* info = nullptr) {
  return loadGrayPreset(text.data(), text.size(), GrayPreset::inkBoyDefaults(), out, info);
}

bool samePreset(const GrayPreset& a, const GrayPreset& b) {
  return memcmp(a.dose, b.dose, sizeof(a.dose)) == 0 && memcmp(a.via, b.via, sizeof(a.via)) == 0 &&
         a.holdMask == b.holdMask && a.retargetDarkPercent == b.retargetDarkPercent &&
         a.retargetLightPercent == b.retargetLightPercent && a.rateIndex == b.rateIndex &&
         a.swapPolarity == b.swapPolarity && a.historyMode == b.historyMode &&
         memcmp(a.returnDelta, b.returnDelta, sizeof(a.returnDelta)) == 0 &&
         memcmp(a.reversePercent, b.reversePercent, sizeof(a.reversePercent)) == 0 &&
         a.weakWhiteToLight == b.weakWhiteToLight;
}

// A preset that differs from the defaults in every field.
GrayPreset changedPreset() {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    p.dose[transitionFrom(i)][transitionTo(i)] = static_cast<uint8_t>(24 - i / 2);  // 24 down to 4
    p.setHold(i, i % 2 == 0);
  }
  p.via[0][1] = 2;
  p.via[6][3] = 0;
  p.via[3][5] = 4;
  p.retargetDarkPercent = 75;
  p.retargetLightPercent = 125;
  p.rateIndex = 2;
  p.swapPolarity = true;
  p.historyMode = 3;
  p.returnDelta[routeIndex(Toward::Dark)] = -11;
  p.returnDelta[routeIndex(Toward::Light)] = 11;
  p.reversePercent[routeIndex(Toward::Dark)] = 250;
  p.reversePercent[routeIndex(Toward::Light)] = 25;
  p.weakWhiteToLight = true;
  return p;
}

// A version 2 array of 42 entries: `fill` everywhere but `index`, which is `value`.
std::string list42(const char* fill, const int index = -1, const char* value = "") {
  std::string out = "[";
  for (int i = 0; i < TRANSITION_COUNT; ++i) {
    if (i > 0) out += ",";
    out += i == index ? value : fill;
  }
  return out + "]";
}

TEST(GrayPreset, SevenLevelsWithTheFourShadesOnTheEvenOnes) {
  EXPECT_EQ(SHADE_COUNT, 7);
  EXPECT_EQ(TRANSITION_COUNT, 42);
  EXPECT_EQ(static_cast<uint8_t>(Shade::K), 0);
  EXPECT_EQ(static_cast<uint8_t>(Shade::D), 2);
  EXPECT_EQ(static_cast<uint8_t>(Shade::L), 4);
  EXPECT_EQ(static_cast<uint8_t>(Shade::W), 6);
  EXPECT_EQ(static_cast<uint8_t>(Shade::TRANSPARENT), 7);
  EXPECT_EQ(VIA_NONE, 7);
  EXPECT_EQ(routeIndex(Toward::Dark), 0u);
  EXPECT_EQ(routeIndex(Toward::Light), 1u);
}

TEST(GrayPreset, TransitionOrderIsFromThenToAscending) {
  uint8_t index = 0;
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      if (from == to) continue;
      EXPECT_EQ(transitionFrom(index), from) << int(index);
      EXPECT_EQ(transitionTo(index), to) << int(index);
      EXPECT_EQ(transitionIndex(from, to), index);
      ++index;
    }
  }
  EXPECT_EQ(index, TRANSITION_COUNT);
  EXPECT_EQ(transitionIndex(0, 1), 0);   // Dose 0>1 first
  EXPECT_EQ(transitionIndex(6, 5), 41);  // Dose 6>5 last
}

TEST(GrayPreset, HoldBitsAreSourceTimesSevenPlusTarget) {
  EXPECT_EQ(holdBit(0, 0), 1ull);
  EXPECT_EQ(holdBit(0, 1), 2ull);
  EXPECT_EQ(holdBit(1, 0), 1ull << 7);
  EXPECT_EQ(holdBit(6, 6), 1ull << 48);
  EXPECT_EQ(HOLD_DIAGONAL, 0x0001010101010101ull);
  EXPECT_EQ(HOLD_ALL, (1ull << 49) - 1);
}

TEST(GrayPreset, DefaultsAreInkBoysTableExpandedToSevenLevels) {
  // The expansion of the digest's table (K>D 2, K>L 4, K>W 7, D>K 4, D>L 1, D>W 6, L>K 4, L>D 1, L>W 5, W>K 7, W>D 3,
  // W>L 2) and of hold 0xc667, dose[from][to] and hold[from][to].
  const uint8_t doses[SHADE_COUNT][SHADE_COUNT] = {
      {0, 1, 2, 3, 4, 6, 7}, {2, 0, 1, 2, 3, 5, 7}, {4, 2, 0, 1, 1, 4, 6}, {4, 3, 1, 0, 1, 3, 6},
      {4, 3, 1, 1, 0, 3, 5}, {6, 4, 2, 2, 1, 0, 3}, {7, 5, 3, 3, 2, 1, 0},
  };
  const bool holds[SHADE_COUNT][SHADE_COUNT] = {
      {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 1, 1, 1, 0}, {0, 0, 1, 1, 1, 1, 0}, {0, 1, 1, 1, 1, 1, 0},
      {0, 1, 1, 1, 1, 0, 0}, {0, 1, 1, 1, 1, 1, 0}, {0, 0, 0, 1, 1, 1, 1},
  };
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      EXPECT_EQ(p.dose[from][to], doses[from][to]) << int(from) << ">" << int(to);
      EXPECT_EQ((p.holdMask & holdBit(from, to)) != 0, holds[from][to]) << int(from) << ">" << int(to);
      EXPECT_EQ(p.via[from][to], VIA_NONE);
    }
  }
  // The anchors are InkBoy's own table and mask exactly.
  const uint8_t v1[4][4] = {{0, 2, 4, 7}, {4, 0, 1, 6}, {4, 1, 0, 5}, {7, 3, 2, 0}};
  for (uint8_t a = 0; a < 4; ++a) {
    for (uint8_t b = 0; b < 4; ++b) {
      EXPECT_EQ(p.dose[2 * a][2 * b], v1[a][b]);
      EXPECT_EQ((p.holdMask & holdBit(2 * a, 2 * b)) != 0, ((0xc667 >> (a * 4 + b)) & 1) != 0);
    }
  }
  EXPECT_EQ(p.retargetDarkPercent, 100);
  EXPECT_EQ(p.retargetLightPercent, 100);
  EXPECT_EQ(p.rateIndex, 0);
  EXPECT_FALSE(p.swapPolarity);
  EXPECT_EQ(p.historyMode, 0);  // history implemented, off by default
  // L>K>L's -1 / 150 % is the return toward light; its mirror D>W>D (0 / 0) the return toward dark.
  EXPECT_EQ(p.returnDelta[routeIndex(Toward::Light)], -1);
  EXPECT_EQ(p.reversePercent[routeIndex(Toward::Light)], 150);
  EXPECT_EQ(p.returnDelta[routeIndex(Toward::Dark)], 0);
  EXPECT_EQ(p.reversePercent[routeIndex(Toward::Dark)], 0);
  EXPECT_FALSE(p.weakWhiteToLight);
  EXPECT_TRUE(p.valid());
}

TEST(GrayPreset, UltraChipDefaultsAreD14sSevenLevelTable) {
  // D14 (2026-10-19), the file's order: from 0: 1 (via 2), 1, 2, 2, 4, 9; from 1: 5, 1, 1, 2, 4, 8; from 2: 7, 2, 1, 1,
  // 3, 8; from 3: 8, 3, 1, 1, 3, 7; from 4: 9, 4, 2, 1, 2, 7; from 5: 10, 5, 3, 2, 1, 5; from 6: 12, 7, 5, 4, 3, 2.
  const uint8_t table[TRANSITION_COUNT] = {1, 1, 2, 2, 4, 9, 5, 1, 1, 2,  4, 8, 7, 2, 1, 1,  3, 8, 8, 3, 1,
                                           1, 3, 7, 9, 4, 2, 1, 2, 7, 10, 5, 3, 2, 1, 5, 12, 7, 5, 4, 3, 2};
  const GrayPreset u = GrayPreset::ultraChipDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    EXPECT_EQ(u.doseAt(i), table[i]) << "transition " << int(i);
    EXPECT_TRUE(u.holdAt(i)) << "transition " << int(i);  // every hold on: one pulse per slot
    EXPECT_EQ(u.viaAt(i), i == transitionIndex(0, 1) ? 2 : VIA_NONE) << "transition " << int(i);
  }
  EXPECT_EQ(u.holdMask, HOLD_ALL);
  for (uint8_t s = 0; s < SHADE_COUNT; ++s) EXPECT_EQ(u.dose[s][s], 0);
  // The four-shade anchors are the Slow-validated prediction: 0>2 1, 0>4 2, 0>6 9, 2>0 7, 2>4 1, 2>6 8, 4>0 9, 4>2 2,
  // 4>6 7, 6>0 12, 6>2 5, 6>4 3.
  const uint8_t anchors[4][4] = {{0, 1, 2, 9}, {7, 0, 1, 8}, {9, 2, 0, 7}, {12, 5, 3, 0}};
  for (uint8_t a = 0; a < 4; ++a) {
    for (uint8_t b = 0; b < 4; ++b) EXPECT_EQ(u.dose[2 * a][2 * b], anchors[a][b]);
  }
  // Everything but the doses, the via and the holds is InkBoy's.
  const GrayPreset ink = GrayPreset::inkBoyDefaults();
  GrayPreset rest = u;
  memcpy(rest.dose, ink.dose, sizeof(rest.dose));
  memcpy(rest.via, ink.via, sizeof(rest.via));
  rest.holdMask = ink.holdMask;
  EXPECT_TRUE(samePreset(rest, ink));
  EXPECT_TRUE(u.valid());
  EXPECT_TRUE(samePreset(presetDefaults(PresetBase::UltraChip), u));
  EXPECT_TRUE(samePreset(presetDefaults(PresetBase::InkBoy), ink));
  EXPECT_STREQ(presetBaseName(PresetBase::UltraChip), "UltraChip");
  EXPECT_STREQ(presetBaseName(PresetBase::InkBoy), "InkBoy's");
}

TEST(GrayPreset, Version1DosesExpandByTheMeanOfTheAnchorTransitions) {
  // A table whose entries differ, so each mean is checked against its own anchors (v1[a][b] is level 2a > 2b).
  const uint8_t v1[4][4] = {{0, 3, 8, 13}, {5, 0, 4, 11}, {9, 6, 0, 7}, {14, 10, 2, 0}};
  uint8_t out[SHADE_COUNT][SHADE_COUNT];
  memset(out, 0xEE, sizeof(out));
  expandVersion1Doses(v1, out);
  for (uint8_t s = 0; s < SHADE_COUNT; ++s) EXPECT_EQ(out[s][s], 0);
  for (uint8_t a = 0; a < 4; ++a) {
    for (uint8_t b = 0; b < 4; ++b) EXPECT_EQ(out[2 * a][2 * b], v1[a][b]);  // the anchors land on 0, 2, 4, 6
  }
  // One odd end: the mean of the two anchor transitions around it, the diagonal counting 0, rounded up.
  EXPECT_EQ(out[0][1], 2);  // (0>0 0 + 0>2 3) / 2 = 1.5
  EXPECT_EQ(out[0][3], 6);  // (0>2 3 + 0>4 8) / 2 = 5.5
  EXPECT_EQ(out[1][0], 3);  // (0>0 0 + 2>0 5) / 2 = 2.5
  EXPECT_EQ(out[5][6], 4);  // (4>6 7 + 6>6 0) / 2 = 3.5
  EXPECT_EQ(out[6][3], 6);  // (6>2 10 + 6>4 2) / 2 = 6
  // Both odd: the mean of the four, rounded up once.
  EXPECT_EQ(out[1][3], 4);   // (0>2 3 + 0>4 8 + 2>2 0 + 2>4 4) / 4 = 3.75
  EXPECT_EQ(out[3][1], 5);   // (2>0 5 + 2>2 0 + 4>0 9 + 4>2 6) / 4 = 5
  EXPECT_EQ(out[5][1], 10);  // (4>0 9 + 4>2 6 + 6>0 14 + 6>2 10) / 4 = 9.75
  EXPECT_EQ(out[1][5], 9);   // (0>4 8 + 0>6 13 + 2>4 4 + 2>6 11) / 4 = 9
}

TEST(GrayPreset, Version1HoldsTakeTheNearestAnchorPair) {
  // One version 1 bit at a time (bit source * 4 + target over K, D, L, W) and the seven-level transitions it reaches:
  // each transition takes the same-direction anchor pair spanning the fewest shades; on a tie (both ends odd), the one
  // whose source lies away from the other end.
  const auto expanded = [](const unsigned v1Bit) { return expandVersion1Holds(static_cast<uint16_t>(1u << v1Bit)); };
  const uint64_t kd = expanded(0 * 4 + 1);
  EXPECT_NE(kd & holdBit(0, 2), 0u);                   // K>D itself
  EXPECT_NE(kd & holdBit(0, 1), 0u);                   // 0>1: 1's anchors 0 and 2, only 0>2 runs the same way
  EXPECT_NE(kd & holdBit(1, 2), 0u);                   // 1>2: only 0>2 (2>2 is diagonal)
  EXPECT_NE(kd & holdBit(0, 3), 0u);                   // 0>3: 0>2 spans one shade, 0>4 two
  EXPECT_NE(kd & holdBit(1, 3), 0u);                   // 1>3: 0>2 and 2>4 tie; the source away from 3 is 0
  EXPECT_EQ(expanded(0 * 4 + 2) & holdBit(1, 3), 0u);  // not K>L (0>4)
  EXPECT_NE(expanded(1 * 4 + 2) & holdBit(1, 4), 0u);  // 1>4: 2>4 (D>L) spans one, 0>4 two
  EXPECT_NE(expanded(2 * 4 + 1) & holdBit(3, 1), 0u);  // 3>1: 2>0 and 4>2 tie; the source away from 1 is 4 (L>D)
  EXPECT_NE(expanded(2 * 4 + 3) & holdBit(5, 6), 0u);  // 5>6 takes L>W (4>6)
  EXPECT_NE(expanded(3 * 4 + 2) & holdBit(6, 5), 0u);  // 6>5 takes W>L (6>4)
  EXPECT_NE(expanded(3 * 4 + 2) & holdBit(6, 3), 0u);  // 6>3: 6>4 spans one, 6>2 two
  // Every transition takes exactly one anchor bit, never a diagonal one, in its own direction.
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      if (from == to) continue;
      int sources = 0;
      for (uint8_t a = 0; a < 4; ++a) {
        for (uint8_t b = 0; b < 4; ++b) {
          if ((expanded(a * 4 + b) & holdBit(from, to)) == 0) continue;
          ++sources;
          EXPECT_NE(a, b);
          EXPECT_EQ(a < b, from < to);
        }
      }
      EXPECT_EQ(sources, 1) << int(from) << ">" << int(to);
    }
  }
  EXPECT_EQ(expandVersion1Holds(0), HOLD_DIAGONAL);
  EXPECT_EQ(expandVersion1Holds(0xFFFF), HOLD_ALL);
}

TEST(GrayPreset, LongestRunSlotsCountsOneDoseASlotForHeldLegsAndBothLegsOfAVia) {
  // InkBoy's expansion: 0>5 6 held (it takes 0>4's bit, K>L).
  EXPECT_EQ(GrayPreset::inkBoyDefaults().longestRunSlots(), 6);
  // UltraChip: every hold on, 6>0 12 the longest (0>1's via is 1 + 2).
  EXPECT_EQ(GrayPreset::ultraChipDefaults().longestRunSlots(), 12);
  GrayPreset p = GrayPreset::ultraChipDefaults();
  p.via[6][5] = 0;  // 6>0 then 0>5: 12 + 4
  EXPECT_EQ(p.longestRunSlots(), 16);
  p.setHold(transitionIndex(6, 0), false);  // that leg two a slot: 6 + 4
  EXPECT_EQ(p.longestRunSlots(), 10);
  p = GrayPreset::inkBoyDefaults();
  const uint8_t wk = transitionIndex(6, 0);  // 6 > 0, hold off
  p.dose[6][0] = DOSE_MAX;
  EXPECT_EQ(p.longestRunSlots(), 12);  // 24 doses, two a slot
  p.setHold(wk, true);
  EXPECT_EQ(p.longestRunSlots(), 24);  // held: one a slot
  p.dose[6][0] = 15;
  p.setHold(wk, false);
  EXPECT_EQ(p.longestRunSlots(), 8);  // odd: ceil(15 / 2), past 0>5's 6
}

TEST(GrayPreset, ValidRejectsEveryOutOfRangeField) {
  const auto broken = [](auto change) {
    GrayPreset p = GrayPreset::inkBoyDefaults();
    change(p);
    return !p.valid();
  };
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[0][1] = 25; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.dose[3][3] = 1; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.holdMask = 0; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.holdMask &= ~holdBit(3, 3); }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.holdMask |= 1ull << 49; }));  // no transition owns it
  EXPECT_TRUE(broken([](GrayPreset& p) { p.via[2][2] = 3; }));           // the diagonal has none
  EXPECT_TRUE(broken([](GrayPreset& p) { p.via[0][1] = 0; }));           // a via is never its from
  EXPECT_TRUE(broken([](GrayPreset& p) { p.via[0][1] = 1; }));           // nor its to
  EXPECT_TRUE(broken([](GrayPreset& p) { p.via[0][1] = 8; }));
  EXPECT_FALSE(broken([](GrayPreset& p) { p.via[0][1] = 2; }));
  EXPECT_FALSE(broken([](GrayPreset& p) { p.via[0][1] = VIA_NONE; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetDarkPercent = 49; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetDarkPercent = 151; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetLightPercent = 49; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.retargetLightPercent = 151; }));
  EXPECT_FALSE(broken([](GrayPreset& p) { p.dose[0][1] = 24; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.historyMode = 4; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.returnDelta[1] = 12; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.returnDelta[0] = -12; }));
  EXPECT_TRUE(broken([](GrayPreset& p) { p.reversePercent[1] = 251; }));
}

TEST(GrayPresetTuning, DoseStepsClampAndTapCycles) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t kw = transitionIndex(0, 6);  // 0 > 6, 7
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
  const uint64_t before = p.holdMask;
  const uint8_t dk = transitionIndex(2, 0);  // 2 > 0, hold off
  ASSERT_FALSE(p.holdAt(dk));
  p.setHold(dk, true);
  EXPECT_TRUE(p.holdAt(dk));
  EXPECT_EQ(p.holdMask, before | holdBit(2, 0));
  p.toggleHold(dk);
  EXPECT_FALSE(p.holdAt(dk));
  EXPECT_EQ(p.holdMask, before);
  p.setHold(dk, false);
  EXPECT_EQ(p.holdMask, before);
  const uint8_t last = transitionIndex(6, 5);  // the highest bit, 6 * 7 + 5
  p.setHold(last, false);
  EXPECT_FALSE(p.holdAt(last));
  p.toggleHold(last);
  EXPECT_NE(p.holdMask & (1ull << 47), 0u);
}

TEST(GrayPresetTuning, ViaStepsThroughNoneAndTheOtherLevels) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t i = transitionIndex(0, 1);
  ASSERT_EQ(p.viaAt(i), VIA_NONE);
  p.stepVia(i, -1);
  EXPECT_EQ(p.viaAt(i), VIA_NONE);          // clamped at none
  const uint8_t order[] = {2, 3, 4, 5, 6};  // never 0 or 1, its own ends
  for (const uint8_t level : order) {
    p.stepVia(i, +1);
    EXPECT_EQ(p.viaAt(i), level);
    EXPECT_TRUE(p.valid());
  }
  p.stepVia(i, +1);
  EXPECT_EQ(p.viaAt(i), 6);  // clamped at the last level
  p.cycleVia(i);
  EXPECT_EQ(p.viaAt(i), VIA_NONE);  // a tap wraps to none
  p.cycleVia(i);
  EXPECT_EQ(p.viaAt(i), 2);
  const uint8_t mid = transitionIndex(3, 5);  // none, 0, 1, 2, 4, 6
  p.cycleVia(mid);
  EXPECT_EQ(p.viaAt(mid), 0);
  p.stepVia(mid, +1);
  p.stepVia(mid, +1);
  p.stepVia(mid, +1);
  EXPECT_EQ(p.viaAt(mid), 4);  // past 3, its from
  p.stepVia(mid, +1);
  EXPECT_EQ(p.viaAt(mid), 6);  // past 5, its to
  EXPECT_TRUE(p.valid());
}

TEST(GrayPresetTuning, HistoryReturnAndReverseStepWithinTheirRanges) {
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.stepHistory(-1);
  EXPECT_EQ(p.historyMode, 0);
  for (int i = 0; i < 5; ++i) p.stepHistory(+1);
  EXPECT_EQ(p.historyMode, 3);
  p.cycleHistory();
  EXPECT_EQ(p.historyMode, 0);
  p.cycleHistory();
  EXPECT_EQ(p.historyMode, 1);
  for (const Toward toward : {Toward::Dark, Toward::Light}) {
    GrayPreset q = GrayPreset::inkBoyDefaults();
    const Toward other = toward == Toward::Dark ? Toward::Light : Toward::Dark;
    const int8_t otherDelta = q.returnDelta[routeIndex(other)];
    const uint8_t otherPercent = q.reversePercent[routeIndex(other)];
    q.returnDelta[routeIndex(toward)] = 0;
    for (int i = 0; i < 20; ++i) q.stepReturnDelta(toward, +1);
    EXPECT_EQ(q.returnDelta[routeIndex(toward)], 11);
    q.cycleReturnDelta(toward);
    EXPECT_EQ(q.returnDelta[routeIndex(toward)], -11);
    q.stepReturnDelta(toward, -1);
    EXPECT_EQ(q.returnDelta[routeIndex(toward)], -11);
    q.cycleReturnDelta(toward);
    EXPECT_EQ(q.returnDelta[routeIndex(toward)], -10);
    q.reversePercent[routeIndex(toward)] = 0;
    q.stepReversePercent(toward, -1);
    EXPECT_EQ(q.reversePercent[routeIndex(toward)], 0);
    for (int i = 0; i < 20; ++i) q.stepReversePercent(toward, +1);
    EXPECT_EQ(q.reversePercent[routeIndex(toward)], 250);
    q.cycleReversePercent(toward);
    EXPECT_EQ(q.reversePercent[routeIndex(toward)], 0);
    q.cycleReversePercent(toward);
    EXPECT_EQ(q.reversePercent[routeIndex(toward)], 25);
    EXPECT_EQ(q.returnDelta[routeIndex(other)], otherDelta);  // the other direction never moves
    EXPECT_EQ(q.reversePercent[routeIndex(other)], otherPercent);
    EXPECT_TRUE(q.valid());
  }
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
  char buffer[1024];
  const size_t length = encodeGrayPreset(GrayPreset::inkBoyDefaults(), PresetBase::InkBoy, buffer, sizeof(buffer));
  EXPECT_EQ(std::string(buffer), DEFAULT_TEXT);
  EXPECT_EQ(length, strlen(DEFAULT_TEXT));
}

TEST(GrayPresetCodec, RoundTripKeepsEveryField) {
  const GrayPreset written = changedPreset();
  ASSERT_TRUE(written.valid());
  char buffer[1024];
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
  EXPECT_FALSE(info.expandedVersion1);                                             // version 2: nothing to expand
  EXPECT_EQ(std::string(buffer).find("\"retarget_percent\""), std::string::npos);  // only the two new keys
  EXPECT_EQ(std::string(buffer).rfind("{\"version\":2,", 0), 0u);
  // The vias as written: -1 for none, the level otherwise.
  EXPECT_NE(std::string(buffer).find("\"via\":[2,-1,"), std::string::npos);
}

TEST(GrayPresetCodec, TheWidestPresetFitsTheFilesEncodeBuffer) {
  // GrayPresetFile's 1,024-byte buffer: every value at its widest.
  GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) {
    p.dose[transitionFrom(i)][transitionTo(i)] = DOSE_MAX;
    p.setHold(i, false);
  }
  p.retargetDarkPercent = 150;
  p.retargetLightPercent = 150;
  p.rateIndex = 255;
  p.swapPolarity = false;
  p.historyMode = 3;
  p.returnDelta[0] = -11;
  p.returnDelta[1] = -11;
  p.reversePercent[0] = 250;
  p.reversePercent[1] = 250;
  p.weakWhiteToLight = false;
  ASSERT_TRUE(p.valid());
  char buffer[1024];
  const size_t length = encodeGrayPreset(p, PresetBase::UltraChip, buffer, sizeof(buffer));
  EXPECT_GT(length, 0u);
  EXPECT_LT(length, 900u);
}

TEST(GrayPresetCodec, TooSmallBufferWritesNothing) {
  const size_t needed = strlen(DEFAULT_TEXT) + 1;
  char buffer[1024];
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
  EXPECT_EQ(load(" {\n  \"version\" : 2 ,\n  \"retarget_dark_percent\": 125\n}\n", out), GrayPresetLoad::Loaded);
  GrayPreset expected = GrayPreset::inkBoyDefaults();
  expected.retargetDarkPercent = 125;
  EXPECT_TRUE(samePreset(out, expected));
  // On an UltraChip panel the absent keys keep the UltraChip doses and their via, in a version 2 or a version 1 file (a
  // version 1 file's vias are cleared only with its own doses).
  for (const char* text : {"{\"version\":2,\"rate\":1}", "{\"version\":1,\"rate\":1}"}) {
    EXPECT_EQ(loadGrayPreset(text, strlen(text), GrayPreset::ultraChipDefaults(), out), GrayPresetLoad::Loaded) << text;
    expected = GrayPreset::ultraChipDefaults();
    expected.rateIndex = 1;
    EXPECT_TRUE(samePreset(out, expected)) << text;
  }
}

TEST(GrayPresetCodec, Version1FileExpandsToSevenLevelsAndSavesAsVersion2) {
  GrayPreset out{};
  GrayPresetLoadInfo info;
  // InkBoy's version 1 file is InkBoy's seven-level defaults exactly: they are its expansion.
  EXPECT_EQ(load(V1_DEFAULT_TEXT, out, &info), GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.expandedVersion1);
  EXPECT_TRUE(samePreset(out, GrayPreset::inkBoyDefaults()));
  EXPECT_EQ(info.droppedVersion1Routes, 0u);  // only 4>0>4 had values
  // The owner's D12 four-shade table over the UltraChip defaults: the doses and holds expand, the routes collapse
  // (L>K>L to toward light, D>W>D to toward dark), and it has no vias.
  const std::string text =
      "{\"version\":1,\"base\":\"ultrachip\",\"doses\":[2,7,12,7,2,10,7,6,9,12,5,3],\"hold\":[false,false,false,"
      "false,false,false,false,false,false,false,false,true],\"return_delta\":[-3,1,2,4,5,6],\"reverse_percent\":[175,"
      "10,50,20,30,40]}";
  EXPECT_EQ(loadGrayPreset(text.data(), text.size(), GrayPreset::ultraChipDefaults(), out, &info),
            GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.expandedVersion1);
  const uint8_t doses[SHADE_COUNT][SHADE_COUNT] = {
      {0, 1, 2, 5, 7, 10, 12}, {4, 0, 1, 3, 5, 8, 11}, {7, 4, 0, 1, 2, 6, 10}, {7, 5, 3, 0, 1, 6, 10},
      {7, 7, 6, 3, 0, 5, 9},   {10, 8, 6, 4, 2, 0, 5}, {12, 9, 5, 4, 3, 2, 0},
  };
  EXPECT_EQ(memcmp(out.dose, doses, sizeof(doses)), 0);
  // W>L (bit 3 * 4 + 2) alone, its nearest pair for 6>4, 6>5, 6>3, 5>4 and 5>3 (5>3: 6>4 and 4>2 tie, the source
  // away from 3 is 6).
  EXPECT_EQ(out.holdMask,
            HOLD_DIAGONAL | holdBit(6, 4) | holdBit(6, 5) | holdBit(6, 3) | holdBit(5, 4) | holdBit(5, 3));
  EXPECT_EQ(out.returnDelta[routeIndex(Toward::Light)], -3);
  EXPECT_EQ(out.returnDelta[routeIndex(Toward::Dark)], 2);
  EXPECT_EQ(out.reversePercent[routeIndex(Toward::Light)], 175);
  EXPECT_EQ(out.reversePercent[routeIndex(Toward::Dark)], 50);
  // No vias: the defaults' 0>1 through 2 is not kept over a table never calibrated for it.
  for (uint8_t i = 0; i < TRANSITION_COUNT; ++i) EXPECT_EQ(out.viaAt(i), VIA_NONE) << int(i);
  // The four routes the collapse dropped had values (2>0>2 1/10, 4>6>4 4/20, 2>4>2 5/30, 4>2>4 6/40): reported for
  // the caller's one log line.
  EXPECT_EQ(info.droppedVersion1Routes, (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5));
  EXPECT_STREQ(version1RouteName(0), "4>0>4");
  EXPECT_STREQ(version1RouteName(2), "2>6>2");
  EXPECT_STREQ(version1RouteName(5), "4>2>4");
  // Saved again: version 2, and it loads back unchanged, with nothing to expand.
  char buffer[1024];
  const size_t length = encodeGrayPreset(out, PresetBase::UltraChip, buffer, sizeof(buffer));
  ASSERT_GT(length, 0u);
  EXPECT_EQ(std::string(buffer).rfind("{\"version\":2,", 0), 0u);
  GrayPreset again{};
  EXPECT_EQ(loadGrayPreset(buffer, length, GrayPreset::inkBoyDefaults(), again, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.expandedVersion1);
  EXPECT_TRUE(samePreset(again, out));
  // A malformed version 1 file reports no expansion.
  info.expandedVersion1 = true;
  EXPECT_EQ(load("{\"version\":1,\"doses\":[1]}", out, &info), GrayPresetLoad::Malformed);
  EXPECT_FALSE(info.expandedVersion1);
}

TEST(GrayPresetCodec, TheVersion1NoticeIsForAnExpansionThatIsNotThePanelsDefaults) {
  GrayPreset out{};
  GrayPresetLoadInfo info;
  // InkBoy's own version 1 file expands to InkBoy's defaults exactly: no notice on the SSD1677.
  ASSERT_EQ(load(V1_DEFAULT_TEXT, out, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(version1NoticeNeeded(info.expandedVersion1, out, PresetBase::InkBoy));
  // Over an UltraChip panel it replaces D14's table: a notice.
  EXPECT_TRUE(version1NoticeNeeded(info.expandedVersion1, out, PresetBase::UltraChip));
  // The owner's four-shade UltraChip file on the UltraChip panel: a notice.
  const std::string owners = "{\"version\":1,\"base\":\"ultrachip\",\"doses\":[2,7,12,7,2,10,7,6,9,12,5,3]}";
  ASSERT_EQ(loadGrayPreset(owners.data(), owners.size(), GrayPreset::ultraChipDefaults(), out, &info),
            GrayPresetLoad::Loaded);
  EXPECT_TRUE(version1NoticeNeeded(info.expandedVersion1, out, PresetBase::UltraChip));
  // A version 2 file never, whatever its values.
  EXPECT_FALSE(version1NoticeNeeded(false, out, PresetBase::UltraChip));
  // A version 1 file without its own arrays over the panel's defaults: they are its values, so no notice.
  ASSERT_EQ(loadGrayPreset("{\"version\":1}", 13, GrayPreset::ultraChipDefaults(), out, &info), GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.expandedVersion1);
  EXPECT_FALSE(version1NoticeNeeded(info.expandedVersion1, out, PresetBase::UltraChip));
  EXPECT_TRUE(samePresetValues(GrayPreset::inkBoyDefaults(), GrayPreset::inkBoyDefaults()));
  EXPECT_FALSE(samePresetValues(GrayPreset::inkBoyDefaults(), GrayPreset::ultraChipDefaults()));
}

TEST(GrayPresetCodec, ArraysOfEitherLayoutMayComeBeforeTheVersion) {
  GrayPreset out{};
  GrayPresetLoadInfo info;
  const std::string v1 = "{\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2],\"version\":1}";
  EXPECT_EQ(load(v1, out, &info), GrayPresetLoad::Loaded);
  EXPECT_TRUE(info.expandedVersion1);
  EXPECT_EQ(memcmp(out.dose, GrayPreset::inkBoyDefaults().dose, sizeof(out.dose)), 0);
  const std::string v2 = "{\"doses\":" + list42("3") + ",\"via\":" + list42("-1", 0, "4") + ",\"version\":2}";
  EXPECT_EQ(load(v2, out, &info), GrayPresetLoad::Loaded);
  EXPECT_FALSE(info.expandedVersion1);
  EXPECT_EQ(out.doseAt(17), 3);
  EXPECT_EQ(out.via[0][1], 4);
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
  char buffer[1024];
  ASSERT_GT(encodeGrayPreset(out, PresetBase::InkBoy, buffer, sizeof(buffer)), 0u);
  EXPECT_NE(std::string(buffer).find("\"retarget_dark_percent\":75,\"retarget_light_percent\":100"), std::string::npos);
  // A malformed file says nothing of the old key.
  info.legacyDark = true;
  EXPECT_EQ(load("{\"version\":1,\"retarget_percent\":125,\"rate\":-1}", out, &info), GrayPresetLoad::Malformed);
  EXPECT_FALSE(info.legacyDark);
  EXPECT_FALSE(info.legacyLight);
}

TEST(GrayPresetCodec, BaseKeyIsWrittenAndReadAndMayBeAbsent) {
  char buffer[1024];
  ASSERT_GT(encodeGrayPreset(GrayPreset::ultraChipDefaults(), PresetBase::UltraChip, buffer, sizeof(buffer)), 0u);
  EXPECT_EQ(std::string(buffer).rfind("{\"version\":2,\"base\":\"ultrachip\",\"doses\":[1,1,2,2,4,9,", 0), 0u);
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
      "{\"version\":0}",
      "{\"version\":3}",
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
      // an array's length against the version (version 1: 12, 12, 6, 6, no via; version 2: 42, 42, 42, 2, 2)
      "{\"version\":2,\"doses\":[2,4,7,4,1,6,4,1,5,7,3,2]}",
      "{\"version\":2,\"hold\":[true,true,false,false,true,false,false,true,false,false,false,true]}",
      "{\"version\":2,\"return_delta\":[-1,0,0,0,0,0]}",
      "{\"version\":2,\"reverse_percent\":[150,0,0,0,0,0]}",
      "{\"version\":1,\"return_delta\":[0,-1]}",
      "{\"version\":1,\"reverse_percent\":[0,150]}",
      "{\"version\":2,\"return_delta\":[0,-1,0]}",
      "{\"version\":2,\"return_delta\":[0,-1,0,0,0,0,0]}",
      "{\"version\":2,\"return_delta\":[]}",
      "{\"version\":2,\"via\":[-1,-1]}",
      // a via: -1 or a level other than both ends; 7 (TRANSPARENT) never
      "{\"version\":2,\"via\":[7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
      "{\"version\":2,\"via\":[8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
      "{\"version\":2,\"via\":[-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
      "{\"version\":2,\"via\":[0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
      "{\"version\":2,\"via\":[1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
      "{\"version\":1,\"via\":[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
      "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]}",
  };
  for (const char* text : cases) {
    GrayPreset out = changedPreset();
    EXPECT_EQ(load(text, out), GrayPresetLoad::Malformed) << text;
    EXPECT_TRUE(samePreset(out, GrayPreset::inkBoyDefaults())) << text;
  }
}

TEST(GrayPresetCodec, DosesUpTo24Load) {
  GrayPreset out{};
  EXPECT_EQ(load("{\"version\":2,\"doses\":" + list42("1", 41, "24") + "}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.doseAt(41), 24);
  EXPECT_EQ(out.doseAt(0), 1);
  EXPECT_EQ(load("{\"version\":2,\"doses\":" + list42("1", 3, "25") + "}", out), GrayPresetLoad::Malformed);
  // A version 1 file's 24s land on the anchors.
  EXPECT_EQ(load("{\"version\":1,\"doses\":[24,1,24,1,24,1,24,1,24,1,24,24]}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.dose[0][2], 24);
  EXPECT_EQ(out.dose[6][4], 24);
  EXPECT_EQ(out.dose[0][4], 1);
}

TEST(GrayPresetCodec, HoldArrayKeepsTheDiagonalBits) {
  GrayPreset out{};
  EXPECT_EQ(
      load("{\"version\":1,\"hold\":[false,false,false,false,false,false,false,false,false,false,false,false]}", out),
      GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_DIAGONAL);
  EXPECT_EQ(load("{\"version\":2,\"hold\":" + list42("false") + "}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_DIAGONAL);
  EXPECT_EQ(load("{\"version\":2,\"hold\":" + list42("true") + "}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_ALL);
  EXPECT_EQ(load("{\"version\":2,\"hold\":" + list42("false", 41, "true") + "}", out), GrayPresetLoad::Loaded);
  EXPECT_EQ(out.holdMask, HOLD_DIAGONAL | holdBit(6, 5));
}

}  // namespace

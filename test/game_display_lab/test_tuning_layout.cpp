// The Tuning screen's sectioned rows (lib/GameDisplayLab/TuningLayout.h, story dlab-g2): the Section row first, then
// the section's rows (the plan's Tuning matrix row: Doses / Holds / Other, the rows below follow).

#include <gtest/gtest.h>

#include <set>

#include "GrayPreset.h"
#include "TuningLayout.h"

namespace {

using namespace GameDisplayLab;

TEST(TuningLayout, TheSectionRowStepsDosesHoldsOtherAndBack) {
  EXPECT_EQ(nextTuningSection(TuningSection::Doses), TuningSection::Holds);
  EXPECT_EQ(nextTuningSection(TuningSection::Holds), TuningSection::Other);
  EXPECT_EQ(nextTuningSection(TuningSection::Other), TuningSection::Doses);
  for (const TuningSection section : {TuningSection::Doses, TuningSection::Holds, TuningSection::Other}) {
    EXPECT_EQ(tuningRowAt(section, 0), TuningRow::Section);
  }
}

TEST(TuningLayout, DosesAndHoldsAreTheFortyTwoTransitionsGroupedBySource) {
  for (const TuningSection section : {TuningSection::Doses, TuningSection::Holds}) {
    const TuningRow kind = section == TuningSection::Doses ? TuningRow::Dose : TuningRow::Hold;
    ASSERT_EQ(tuningRowCount(section), 43);
    for (int row = 1; row < tuningRowCount(section); ++row) {
      EXPECT_EQ(tuningRowAt(section, row), kind) << row;
      EXPECT_EQ(tuningTransitionAt(section, row), row - 1) << row;
    }
    // Dose 0>1 first, then 0>2 .. 0>6, 1>0, ..., Dose 6>5 last.
    EXPECT_EQ(transitionFrom(tuningTransitionAt(section, 1)), 0);
    EXPECT_EQ(transitionTo(tuningTransitionAt(section, 1)), 1);
    EXPECT_EQ(transitionFrom(tuningTransitionAt(section, 7)), 1);
    EXPECT_EQ(transitionTo(tuningTransitionAt(section, 7)), 0);
    EXPECT_EQ(transitionFrom(tuningTransitionAt(section, 42)), 6);
    EXPECT_EQ(transitionTo(tuningTransitionAt(section, 42)), 5);
  }
}

TEST(TuningLayout, OtherHoldsTheRetargetsTheRoutesTheViasThenTheRest) {
  const TuningSection other = TuningSection::Other;
  ASSERT_EQ(tuningRowCount(other), TUNING_MAX_ROWS);
  EXPECT_EQ(TUNING_MAX_ROWS, 54);
  const TuningRow head[] = {TuningRow::Section,     TuningRow::RetargetDark, TuningRow::RetargetLight,
                            TuningRow::History,     TuningRow::ReturnDark,   TuningRow::ReturnLight,
                            TuningRow::ReverseDark, TuningRow::ReverseLight};
  for (int row = 0; row < 8; ++row) EXPECT_EQ(tuningRowAt(other, row), head[row]) << row;
  std::set<int> vias;
  for (int row = 8; row < 8 + TRANSITION_COUNT; ++row) {
    EXPECT_EQ(tuningRowAt(other, row), TuningRow::Via) << row;
    vias.insert(tuningTransitionAt(other, row));
  }
  EXPECT_EQ(vias.size(), static_cast<size_t>(TRANSITION_COUNT));
  EXPECT_EQ(*vias.begin(), 0);
  EXPECT_EQ(*vias.rbegin(), TRANSITION_COUNT - 1);
  EXPECT_EQ(tuningRowAt(other, 50), TuningRow::Rate);
  EXPECT_EQ(tuningRowAt(other, 51), TuningRow::Swap);
  EXPECT_EQ(tuningRowAt(other, 52), TuningRow::Reset);
  EXPECT_EQ(tuningRowAt(other, 53), TuningRow::Save);
  EXPECT_EQ(TUNING_OTHER_SAVE_ROW, 53);
}

}  // namespace

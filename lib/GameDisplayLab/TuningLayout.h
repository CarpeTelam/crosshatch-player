#pragma once

#include <cstdint>

#include "GrayPreset.h"

// The Tuning screen's rows (story dlab-g2: seven levels make 42 transitions, so the rows come in three sections behind
// a Section row), pure and pinned by test/game_display_lab/test_tuning_layout.cpp; TuningActivity draws them. Row 0 is
// always the Section row. Doses: the 42 dose rows in the transition order (grouped by source level, 0>1 .. 6>5).
// Holds: the 42 hold rows in the same order. Other: the two retargets, then the routes (History, the two returns, the
// two reverses, the 42 vias), then Frame rate, Swap polarity, Reset to defaults and Save.
namespace GameDisplayLab {

enum class TuningSection : uint8_t { Doses, Holds, Other };

enum class TuningRow : uint8_t {
  Section,
  Dose,
  Hold,
  RetargetDark,
  RetargetLight,
  History,
  ReturnDark,
  ReturnLight,
  ReverseDark,
  ReverseLight,
  Via,
  Rate,
  Swap,
  Reset,
  Save,
};

// Other's fixed rows before its vias, after the Section row: the two retargets, History, the two returns, the two
// reverses.
constexpr int TUNING_OTHER_VIA_FIRST = 8;
constexpr int TUNING_OTHER_RATE_ROW = TUNING_OTHER_VIA_FIRST + TRANSITION_COUNT;
constexpr int TUNING_OTHER_SAVE_ROW = TUNING_OTHER_RATE_ROW + 3;
// The longest section's rows (Other's, the Section row included).
constexpr int TUNING_MAX_ROWS = TUNING_OTHER_SAVE_ROW + 1;

// The Section row's next section: Doses, Holds, Other, then Doses again.
constexpr TuningSection nextTuningSection(const TuningSection section) {
  switch (section) {
    case TuningSection::Doses:
      return TuningSection::Holds;
    case TuningSection::Holds:
      return TuningSection::Other;
    case TuningSection::Other:
      return TuningSection::Doses;
  }
  return TuningSection::Doses;
}

// The rows `section` shows, the Section row included.
constexpr int tuningRowCount(const TuningSection section) {
  return section == TuningSection::Other ? TUNING_MAX_ROWS : 1 + TRANSITION_COUNT;
}

// Row `row` (0 .. tuningRowCount - 1) of `section`.
constexpr TuningRow tuningRowAt(const TuningSection section, const int row) {
  if (row == 0) return TuningRow::Section;
  if (section == TuningSection::Doses) return TuningRow::Dose;
  if (section == TuningSection::Holds) return TuningRow::Hold;
  constexpr TuningRow FIXED[] = {TuningRow::RetargetDark, TuningRow::RetargetLight, TuningRow::History,
                                 TuningRow::ReturnDark,   TuningRow::ReturnLight,   TuningRow::ReverseDark,
                                 TuningRow::ReverseLight};
  if (row < TUNING_OTHER_VIA_FIRST) return FIXED[row - 1];
  if (row < TUNING_OTHER_RATE_ROW) return TuningRow::Via;
  if (row == TUNING_OTHER_RATE_ROW) return TuningRow::Rate;
  if (row == TUNING_OTHER_RATE_ROW + 1) return TuningRow::Swap;
  if (row == TUNING_OTHER_RATE_ROW + 2) return TuningRow::Reset;
  return TuningRow::Save;
}

// The transition (0..41) of a dose, hold or via row of `section`.
constexpr uint8_t tuningTransitionAt(const TuningSection section, const int row) {
  return static_cast<uint8_t>(row - (section == TuningSection::Other ? TUNING_OTHER_VIA_FIRST : 1));
}

}  // namespace GameDisplayLab

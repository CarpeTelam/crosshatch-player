#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayPreset.h>

#include <cstddef>
#include <cstdint>

// The tuning screen (Display lab → Tuning; the display-lab epic, R4): the session's gray preset
// (DisplayLabActivity::preset()), one value per row, each row its name over its current value: the 12 doses ("Dose
// K>D", 1..24), the 12 hold bits ("Hold K>D", on or off), Retarget to dark and Retarget to light (50..150 % each, the
// budget of a pixel turned mid-transition toward a darker or a lighter shade), Frame rate (the backend's rate label),
// Swap polarity, and Save (writes the preset file; its line says whether that worked). Edits apply to the
// session at once, so the next transition-test sweep uses them; nothing is written until Save (here or on the lab
// menu).
//
// A tap on a value row steps it in place (+1, wrapping; holds and the swap toggle), as GameOptionsActivity does.
// Confirm on a value row enters edit mode for it (its line says so): NavNext and NavPrevious then change the value by
// one step (+1 / -1, clamped; a hold or the swap on / off), and Confirm or Back leaves edit mode. Outside edit mode
// NavNext and NavPrevious move the selection, as on every list (the X4 Pro's Up and Down), and Back closes the screen.
// Every change is logged as `DLAB tune <row>=<value>`.
class TuningActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabTuning";
  // 12 doses, 12 holds, Retarget to dark, Retarget to light, Frame rate, Swap polarity, Save.
  static constexpr int DOSE_FIRST = 0;
  static constexpr int HOLD_FIRST = DOSE_FIRST + GameDisplayLab::TRANSITION_COUNT;
  static constexpr int RETARGET_DARK_ROW = HOLD_FIRST + GameDisplayLab::TRANSITION_COUNT;
  static constexpr int RETARGET_LIGHT_ROW = RETARGET_DARK_ROW + 1;
  static constexpr int RATE_ROW = RETARGET_LIGHT_ROW + 1;
  static constexpr int SWAP_ROW = RATE_ROW + 1;
  static constexpr int SAVE_ROW = SWAP_ROW + 1;
  static constexpr size_t ROW_COUNT = SAVE_ROW + 1;

  // `lab` is the menu under this screen on the stack, which outlives it.
  TuningActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  enum class Kind : uint8_t { Dose, Hold, RetargetDark, RetargetLight, Rate, Swap, Save };
  static constexpr int NOT_EDITING = -1;

  int listCount() const override { return static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  // A tap: steps the row in place, or saves.
  void activateIndex(int index) override;
  // In edit mode, NavNext and NavPrevious change the value.
  bool handleCustomInput() override;
  // Confirm enters or leaves edit mode (or saves), Back leaves edit mode or the screen.
  bool handleButtons() override;
  // No selection moves in edit mode.
  void navigateButtons() override;
  void onBackButton() override;

  static Kind kindOf(int row);
  // The transition (0..11) of a dose or hold row.
  static uint8_t transitionOf(int row);
  void buildRows();
  // Row `row`'s value as text ("7", "On", "100%", the rate label); false for the Save row, which has none.
  bool plainValue(int row, char* out, size_t capacity) const;
  // Writes row `row`'s value line (with the edit mark when it is being edited). The caller holds the render lock.
  void writeValue(int row);
  // Applies `change` to the preset, rewrites the row's line, logs it, and repaints.
  template <typename Change>
  void edit(int row, Change change);
  // A tap's step and an edit-mode step (`direction` +1 or -1).
  void tapStep(int row);
  void adjust(int row, int direction);
  void setEditing(int row);
  void save();
  uint8_t rateCount() const;

  DisplayLabActivity& lab;
  // The row in edit mode, or NOT_EDITING.
  int editing = NOT_EDITING;
  char labels[2 * GameDisplayLab::TRANSITION_COUNT][16] = {};
  char values[ROW_COUNT][40] = {};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
};

#endif  // CROSSHATCH_DISPLAY_LAB

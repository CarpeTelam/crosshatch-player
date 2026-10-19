#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayPreset.h>
#include <TuningLayout.h>

#include <cstddef>
#include <cstdint>

// The tuning screen (Display lab → Tuning; the display-lab epic, R4): the session's gray preset
// (DisplayLabActivity::preset()), one value per row, each row its name over its current value. Seven levels (story
// dlab-g2, D14) make 42 transitions, so the rows come in three sections and a Section row at the top picks which one
// the rows below it show (Doses on every visit, a lab preference, never saved; a tap or Confirm steps it Doses, Holds,
// Other):
// - Doses: the 42 doses, grouped by source level ("Dose 0>1" .. "Dose 6>5", 1..24; levels by their digit, 0 black, 3
//   the 50 % gray, 6 white: GameDisplayLab::shadeName).
// - Holds: the 42 hold bits in the same order ("Hold 0>1", on or off).
// - Other: Retarget to dark and Retarget to light (50..150 % each, the budget of a pixel turned mid-transition toward a
//   darker or a lighter level), then the routes group: History (off, returns, reversals or both), Return to dark and
//   Return to light (the dose change of a completed return in that direction, -11..11), Reverse to dark and Reverse to
//   light (a reversal's percent, 0..250, 0 meaning as the retarget), and the 42 vias ("Via 0>1": None or the level the
//   transition passes through); then Frame rate (the backend's rate label), Swap polarity, Reset to defaults (the
//   controller's defaults into the session at once, logged as `DLAB tune reset`; the only on-device way back to them),
//   and Save (writes the preset file; its line says whether that worked).
// Edits apply to the session at once, so the next transition-test sweep uses them; nothing is written until Save (here
// or on the lab menu).
//
// A tap on a value row steps it in place (+1, wrapping; holds and the swap toggle), as GameOptionsActivity does.
// Confirm on a value row enters edit mode for it (its line says so): NavNext and NavPrevious then change the value by
// one step (+1 / -1, clamped; a hold or the swap on / off), and Confirm or Back leaves edit mode. Outside edit mode
// NavNext and NavPrevious move the selection, as on every list (the X4 Pro's Up and Down), and Back closes the screen.
// Every change is logged as `DLAB tune <row>=<value>`.
class TuningActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabTuning";
  // The longest section's rows (GameDisplayLab::TuningLayout.h).
  static constexpr size_t MAX_ROWS = GameDisplayLab::TUNING_MAX_ROWS;

  // `lab` is the menu under this screen on the stack, which outlives it.
  TuningActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  using Row = GameDisplayLab::TuningRow;
  using Section = GameDisplayLab::TuningSection;
  static constexpr int NOT_EDITING = -1;

  int listCount() const override { return rowCount; }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  // A tap: steps the row in place, or saves.
  void activateIndex(int index) override;
  // In edit mode, NavNext and NavPrevious change the value.
  bool handleCustomInput() override;
  // Confirm enters or leaves edit mode (or saves, resets or steps the section), Back leaves edit mode or the screen.
  bool handleButtons() override;
  // No selection moves in edit mode.
  void navigateButtons() override;
  void onBackButton() override;

  // Row `row`'s kind in the current section (GameDisplayLab::tuningRowAt).
  Row kindOf(int row) const { return GameDisplayLab::tuningRowAt(section, row); }
  // The transition (0..41) of a dose, hold or via row.
  uint8_t transitionOf(int row) const { return GameDisplayLab::tuningTransitionAt(section, row); }
  // The current section's rows, labels and lines. The caller holds the render lock.
  void buildRows();
  // Row `row`'s value as text ("7", "On", "100%", the rate label); false for the action rows, which have none.
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
  // The Section row: the next section's rows, the selection back on the Section row.
  void nextSection();
  void save();
  // The controller's defaults into the session preset (DisplayLabActivity::resetPreset), every row rewritten.
  void reset();
  uint8_t rateCount() const;

  DisplayLabActivity& lab;
  Section section = Section::Doses;
  int rowCount = 0;
  // The row in edit mode, or NOT_EDITING.
  int editing = NOT_EDITING;
  // The Save row's line after a save (null: its description).
  const char* saveLine = nullptr;
  char labels[MAX_ROWS][16] = {};
  // A row's value line, and the plain value inside it (the Section row's Other is the longest, 35 characters).
  static constexpr size_t VALUE_CHARS = 40;
  char values[MAX_ROWS][VALUE_CHARS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
};

#endif  // CROSSHATCH_DISPLAY_LAB

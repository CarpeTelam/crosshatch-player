#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <LabExperiments.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// The polarity screen (Display lab → Polarity): the second unknown. Headed "Polarity", two rows, one per drive row:
// Row 1 (the white mask; the BW-plane row, LUT1, on the SSD1677) and Row 2 (the black mask; the RED-plane row, LUT2),
// each labelled with the code it is driven with, VSL and VSH1 (GameDisplayLab::POLARITY_ROW1/2, InkBoy's SSD1681
// convention as the first guess). Activating a row sets every pixel of that mask, clears the other, and scans with
// TP[0A] = 10 at rate index 0 through the lab's scan(), logged as
// `DLAB polarity row=<n> vs=<code> ms=<ms>`; the panel then shows the drive (darker or lighter all over), and the
// screen is not repainted until the next Confirm or tap, so the user can look: render() draws nothing while the drive
// is shown (a routed tap requests a repaint on its own, which must not wipe the panel), and Up/Down move the selection
// without a repaint. That next Confirm or tap repaints with a FULL refresh (which cleans the panel) and does nothing
// else; the one after runs the row it lands on. The row's line says how long the drive took. Which row darkened is the
// answer, noted by hand (docs/crosshatch/display-lab.md).
class PolarityActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabPolarity";
  static constexpr size_t ROW_COUNT = GameDisplayLab::POLARITY_ROWS;

  // `lab` is the menu under this screen on the stack, which outlives it.
  PolarityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  int listCount() const override { return static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  // Draws nothing while the panel shows a drive; the base otherwise.
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  void buildRows();
  // Drives the whole panel with LUT row `index` + 1.
  void drive(size_t index);

  DisplayLabActivity& lab;
  // Each row's label ("Row 1 (BW plane, VSL)") and line: not run, "Driven in N ms", or a status.
  char labels[ROW_COUNT][40] = {};
  char results[ROW_COUNT][80] = {};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  // The panel shows a drive and the screen has not been repainted since: render() draws nothing and the next
  // activation repaints only. Written by the loop task, read by the render task.
  std::atomic<bool> shownOnPanel{false};
};

#endif  // CROSSHATCH_DISPLAY_LAB

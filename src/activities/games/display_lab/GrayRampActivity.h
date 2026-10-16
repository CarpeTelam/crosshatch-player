#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalMemory.h>

#include <atomic>
#include <cstdint>

// The gray-ramp screen (Display lab → Gray ramp, UltraChip stock-path bench, story dlab-u1): the controller's OEM
// four-level grayscale through the facade's overlay masks. Headed "Gray ramp", one row whose line is the status.
//
// Activating it paints four bands, black, dark gray, light gray, white, running left to right as the user holds the
// device (GameDisplayLab::fillGrayRamp into the framebuffer and two PSRAM mask planes, oriented by the renderer's
// orientation), as the reader paints a gray page: displayGrayscaleBase(FAST) timed as the base, the plane upload and
// displayGrayBuffer timed as the gray pass, then cleanupGrayscaleBuffers; logged as `DLAB gray base_ms=<> gray_ms=<>`.
// The panel then keeps the ramp: render() draws nothing until the next Confirm or tap, which only repaints the screen
// with a FULL refresh (PolarityActivity's rule, the epic's D4). A panel that reports no overlay grayscale gets a notice
// line and no pass (`DLAB gray unsupported`); an inverted display is refused per activation; a call over
// DisplayLabActivity::SCAN_TIMEOUT_MS is logged and the screen comes back at once with a FULL refresh. Every way out
// after a pass this visit runs one FULL refresh (Back promotes it onto the menu's repaint, Home and sleep run it in
// onExit).
class GrayRampActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabGrayRamp";

  // `lab` (the menu under this screen) is unused: the stock bench has no scan session.
  GrayRampActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  int listCount() const override { return 1; }
  const char* headerTitle() const override;
  void onEnter() override;
  // Runs the cleaning FULL refresh when a pass drove the panel and Back did not promote it; frees the planes. The
  // render mutex is already held by the caller.
  void onExit() override;
  // Draws nothing while the panel holds the ramp; the base otherwise.
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  // The status line from `text`, and the row that shows it.
  void setStatus(const char* text);
  // One timed ramp: the checks, the base and the gray pass, and the hold.
  void paint();

  // The ramp's two overlay masks (LSB, MSB), each the framebuffer's size. PSRAM, onEnter to onExit.
  HalMemory::PsramBuffer lsbPlane;
  HalMemory::PsramBuffer msbPlane;
  // The row's line: "Not run", the timings, or a status.
  char status[96] = {};
  freeink::ui::ListItem rowItem = {};
  // A pass sent something to the panel this visit and no FULL refresh for leaving has been arranged yet.
  bool drovePanel = false;
  // The panel shows the ramp and the screen has not been repainted since: render() draws nothing and the next
  // activation repaints only. Written by the loop task, read by the render task.
  std::atomic<bool> held{false};
};

#endif  // CROSSHATCH_DISPLAY_LAB

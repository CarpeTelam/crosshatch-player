#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayModel.h>
#include <HalMemory.h>
#include <MaskPlanes.h>
#include <SlotScheduler.h>
#include <TransitionScene.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// The transition test (Display lab → Transition test; the display-lab epic, R4; InkBoy's calibration screen): the
// gray engine on the whole panel. Headed "Transition test", three rows: Run (its line: "Not run", or how the last run
// ended), Speed (the last "N.N scans/s, N ms per scan"), Controls (help text).
//
// Run cleans the panel to white with a FULL refresh and starts the sweep (GameDisplayLab::TransitionScene): four shade
// bands K, D, L, W top to bottom, three 12-px rectangles of the other shades per band crossing the screen in 5 s, a
// 1 s pause, repeat, driven from white through the session's gray preset (DisplayLabActivity::preset()). One slot per
// GameDisplayLab::SlotScheduler period: the scene moves the rectangles to the sweep clock, then
// GameDisplayLab::runGraySlot sends the drive scan and the follow-up scan through DisplayLabActivity::scan and commits
// what was delivered. While the test view is up the screen draws nothing (D4: the panel shows only what the scans
// drive); Confirm or a tap over the rows' area pauses or resumes, a swipe does nothing, NavNext replays (clean, sweep
// from 0), NavPrevious cleans (white panel, paused), Back leaves the view. A non-Done scan stops the sweep and leaves
// the view with its outcome on the Run row, and another screen covering it (the light panel) ends it once it closes;
// a forced refresh is swallowed. Leaving the view repaints the rows with a FULL refresh. Before a slot that
// would cross the session cap the screen cleans first (white panel, model reset, the scene reapplied; the sweep clock
// stands still through it), so the lab's own cap clean never lands between a slot's two scans. Logs `DLAB gray
// scans_s=N.N ms_per_scan=N skipped=N` (skipped: slot periods lost in that window) about every 2 s while running and
// on every stop.
//
// Memory, all PSRAM, allocated in onEnter and freed in onExit: the model (2 B a pixel, 768,000 B at 800x480), the
// follow-up masks (2 x 48,000 B), the history (3 B a pixel, 1,152,000 B) only when the preset's history mode is on,
// and the weak bitmap (48,000 B) only when weak mode is on and the backend has a weak row. When any does not fit, the
// screen shows one notice row and runs nothing.
class TransitionTestActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabTransition";
  // How often the running sweep logs and shows its speed.
  static constexpr uint32_t STATS_EVERY_US = 2000000;
  // Scans in one slot: the drive scan and the follow-up.
  static constexpr uint32_t SCANS_PER_SLOT = 2;
  // A gap between two loop() calls this long while the test view is up means another screen covered it.
  static constexpr uint32_t COVERED_GAP_US = 1000000;

  // `lab` is the menu under this screen on the stack, which outlives it.
  TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

  void loop() override;
  bool skipLoopDelay() override { return running; }
  bool preventAutoSleep() override { return running; }
  // A forced refresh (the power or home button's Refresh) would send the framebuffer over the drive: swallowed while
  // the test view is up (D4).
  bool handleForcedRefresh() override { return shownOnPanel.load(); }

 private:
  enum class Row : uint8_t { Run, Stats, Controls };
  static constexpr size_t ROW_COUNT = 3;

  int listCount() const override { return notice ? 1 : static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  // Frees every buffer; the render mutex is already held by the caller (no RenderLock here).
  void onExit() override;
  // Draws nothing while the test view is up; the base otherwise.
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The test view's buttons: Confirm, NavNext, NavPrevious, Back; nothing in the rows view.
  bool handleCustomInput() override;
  // No selection moves while the test view is up.
  void navigateButtons() override;
  void onBackButton() override;

  // The model, the follow-up masks and the optional arrays; false, having logged why, when one has no room.
  bool allocate();
  void release();
  void buildRows();
  // Clean, reset, the scene from 0, running: Run and NavNext.
  void startRun();
  // White framebuffer, the lab's cleaning FULL refresh, the model reset to white, the scene to reapply. The caller
  // holds the render lock.
  void cleanToWhite();
  void togglePause();
  // NavPrevious: a white panel, paused.
  void cleanAndPause();
  // Back to the rows with `outcome` on the Run row, repainted with a FULL refresh.
  void leaveView(const char* outcome);
  // One slot at `nowUs`.
  void runSlot(uint32_t nowUs);
  // The sweep clock advanced to `nowUs` (it runs only while running).
  void advanceClock(uint32_t nowUs);
  // Logs the window's speed (`DLAB gray scans_s=...`).
  void logStats(uint32_t nowUs) const;
  // Logs the speed and writes the Speed line (takes the render lock), then starts a new window.
  void reportStats(uint32_t nowUs);

  DisplayLabActivity& lab;
  HalMemory::PsramBuffer pixelBuffer;
  HalMemory::PsramBuffer followBlack;
  HalMemory::PsramBuffer followWhite;
  HalMemory::PsramBuffer historyBuffer;
  HalMemory::PsramBuffer weakBuffer;
  GameDisplayLab::GrayModel model;
  GameDisplayLab::MaskPlanes follow;
  GameDisplayLab::TransitionScene scene;
  GameDisplayLab::SlotScheduler scheduler;
  // The weak row is in use: weak mode on and the backend has one (and its bitmap fitted).
  bool weakRow = false;
  // Why the screen cannot run (its one row), or null.
  const char* notice = nullptr;
  // The Run row's line and the speed line.
  const char* status = nullptr;
  char statsText[48] = {};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  // The test view is up: the panel shows the drive and render() draws nothing. Written by the loop task, read by the
  // render task.
  std::atomic<bool> shownOnPanel{false};
  // The sweep advances (the test view is up and not paused).
  bool running = false;
  // The model must be retargeted to the whole scene before the next slot (after a clean).
  bool sceneStale = false;
  // The sweep clock, which stands still while paused, the micros() it was last advanced at, and the scene time the
  // model was last moved to.
  uint64_t elapsedUs = 0;
  uint32_t clockUs = 0;
  uint32_t sceneMs = 0;
  uint32_t statsSinceUs = 0;
  // micros() at the end of the last loop() call.
  uint32_t lastLoopUs = 0;
};

#endif  // CROSSHATCH_DISPLAY_LAB

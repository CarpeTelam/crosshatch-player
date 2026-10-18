#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayModel.h>
#include <GrayPicture.h>
#include <GrayPreset.h>
#include <GraySlot.h>
#include <ImageRows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "GrayImageReader.h"
#include "GrayRunner.h"

// The gray viewer (Display lab → Gray viewer; story dlab-g3, the epic's D16 and its amendment): a picture painted
// through the gray engine, to see how its shades look on real artwork, line art first. Headed "Gray viewer", six rows:
// Image (the entry the next run paints, "name (n of N)"; a tap or Confirm steps it: the test chart first, then the
// PNG and JPEG files of IMAGES_DIR sorted by name, then the chart again), Dither (Plain, the nearest level, or Ordered,
// the 4x4 Bayer matrix scaled to the level count; a tap or Confirm toggles it), Run (paints the entry), Levels (the
// engine's level count and names, GameDisplayLab::SHADE_COUNT and shadeName: four today, seven after dlab-g2 with no
// change here), Controls (help text), Status (how the last run ended: "Not run", "Stopped", a scan's outcome, or why
// the picture could not be read).
//
// Run starts the shared sweep runner (GrayRunner, the transition test's: its task, slots, stops, cover and session
// cap). Its task cleans the panel to white with a FULL refresh, then paints the entry into the model: the chart
// (GameDisplayLab::testChartGray, the same pixels on every device) or the file, decoded on the task by readGrayImage
// (PNGdec or JPEGDEC directly, 8-bit gray rows fitted to the logical screen, white letterbox), each row quantised with
// the Dither row (GameDisplayLab::GrayPicture::paintGrayRow: one GrayModel::retargetRect per run of equal targets).
// The renderer's framebuffer is never drawn (D4; the clean's white fill is the lab's own clean). Slots then run until
// one drives no pixel (the paint settled: `gray viewer: <name> painted after N slots, pixels per level [...]`), the
// hold marker (a pause symbol at the logical bottom-right, driven to black or white, whichever is farther from the
// picture under it) is driven out, and once it has settled the run holds. While it paints, NavNext (side Down),
// Confirm, or a tap where touch routes while the view is up pauses or resumes, as in the transition test; while it
// holds they paint the next entry (clean first; after the last file, the chart), which the Image row then shows, and
// further presses are ignored until it is painted, as during the run's first paint (GrayRunner::press, one step on the
// request word); a resume that ever reaches a held picture holds again (keepHolding). NavPrevious (side Up)
// or Back stops; the rows come back with a FULL refresh. A file that cannot be read ends the run with the reason on the
// Status row (logged with the file's name); the Image row still steps, and the chart still works. The session cap's
// clean drops the hold in progress and paints the entry again (the file decoded again).
//
// Memory, allocated in onEnter and freed in onExit: the runner's working set (PSRAM), two logical rows of scratch and
// the file list (MAX_IMAGES names of NAME_BYTES, the first by name; internal RAM, small). A decode adds the decoder
// (PNGdec about 59 KB with the firmware's PNG row buffer, JPEGDEC about 20 KB, heap, as the converters allocate them)
// and its row buffers (PNG: a gray row, two more when it enlarges vertically; JPEG: two strip rows when it enlarges
// vertically) while it runs. The task's stack (VIEWER_STACK_BYTES, internal RAM)
// exists only while a run is up.
class GrayViewerActivity final : public UiListActivity, private GrayRunner::Client {
 public:
  static constexpr const char* NAME = "DlabGrayViewer";
  static constexpr char IMAGES_DIR[] = "/.crosshatch/display-lab/images";
  // The files the Image row lists (the first, by name, when there are more) and the longest name it keeps, with its
  // NUL; a longer name is skipped (logged).
  static constexpr size_t MAX_IMAGES = 32;
  static constexpr size_t NAME_BYTES = GameDisplayLab::IMAGE_NAME_BYTES;
  // The sweep task's stack, provisional. The decode chain (PNGdec's inflate or JPEGDEC's MCU decode, the row callback,
  // the quantiser and a retarget) and the scan chain (the transition test's measured 8 KiB provision,
  // TransitionTestActivity::SWEEP_STACK_BYTES) never nest, so the need is the larger of the two, not their sum; the
  // converters run the decode chain on the stock 8 KiB loop stack beneath an activity's own frames. 12 KiB leaves room
  // over either until the measured high-water marks the story's Verification asks for (a PNG, a baseline and a
  // progressive JPEG, the run's `gray sweep task ended` line) set it.
  static constexpr uint32_t VIEWER_STACK_BYTES = 12288;

  // `lab` is the menu under this screen on the stack, which outlives it.
  GrayViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

  void loop() override;
  // As the transition test: the loop keeps its delay; the runner keeps the device awake while it paints and for
  // GrayRunner::HOLD_AWAKE_MS of a hold.
  bool skipLoopDelay() override { return false; }
  bool preventAutoSleep() override { return runner && runner->preventAutoSleep(); }
  // Ends the sweep task (the render mutex is held: GrayRunner::endLocked).
  void prepareForSleep() override;
  // Stops and joins the sweep task; false, so Home goes on (onExit promotes the FULL refresh).
  bool handleHomeGesture() override;
  // A forced refresh would send the framebuffer over the drive: swallowed while the view is up (D4).
  bool handleForcedRefresh() override { return shownOnPanel.load(); }

 private:
  enum class Row : uint8_t { Image, Dither, Run, Levels, Controls, Status };
  static constexpr size_t ROW_COUNT = 6;

  // The reader's rows, painted into the model on the sweep task. It holds the runner (handed to the hook; a heap object
  // a detached task keeps) for its stop check, never the screen: a stop is checked before every row, so a task
  // returning after the screen is gone asks only the runner; the picture and the shade row are touched only by rows,
  // which a stop ends.
  class RowSink final : public GrayRowSink {
   public:
    RowSink(GrayRunner& runner, const GameDisplayLab::GrayPicture& picture, GameDisplayLab::Dither dither,
            uint8_t* shades, GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset)
        : runner(runner), picture(picture), dither(dither), shades(shades), model(model), preset(preset) {}
    void row(int y, int x0, const uint8_t* gray, int count) override;
    bool stopped() const override { return runner.stopRequested(); }

   private:
    GrayRunner& runner;
    const GameDisplayLab::GrayPicture& picture;
    GameDisplayLab::Dither dither;
    uint8_t* shades;
    GameDisplayLab::GrayModel& model;
    const GameDisplayLab::GrayPreset& preset;
  };

  int listCount() const override { return notice ? 1 : static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  // Ends the sweep task and frees every buffer; the render mutex is already held by the caller (no RenderLock here).
  void onExit() override;
  // Draws nothing while the view is up; the base otherwise.
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The view's buttons: NavNext and Confirm pause, resume or go to the next picture; NavPrevious and Back stop.
  bool handleCustomInput() override;
  // No selection moves while the view is up.
  void navigateButtons() override;
  void onBackButton() override;

  // ---- loop task --------------------------------------------------------------------------------------------------
  // The files of IMAGES_DIR into `images` (GameDisplayLab::ImageList: sorted, at most MAX_IMAGES), before the render
  // lock; none when the folder is absent or empty, or the list had no room (logged).
  void listImages();
  // The scratch rows and the runner's working set; false (logged) when one has no room.
  bool allocate();
  void release();
  void buildRows();
  // The Image row's line for the current entry.
  void formatImageText();
  // The run's view up and the task created (it cleans and paints the entry).
  void startRun();
  // NavNext, Confirm or a tap while the view is up: the next picture while held (ignored while one is being painted),
  // otherwise pause or resume.
  void nextOrPause();
  // The Image row: the next entry, after the last the chart.
  void stepImage();
  // The Dither row: Plain and Ordered.
  void toggleDither();
  // The Status row's line for how the task's run ended.
  const char* endText() const;
  // After a stop: endText() when the task was joined, "Still stopping" when it was left running past the bound.
  const char* stopText(bool joined) const;
  // Why no run starts while a sweep task is still ending: a restart when one was detached, else "Still stopping".
  const char* blockedText() const;
  // Back to the rows, repainted with a FULL refresh, with how the run ended on the Status row when the task was
  // `joined`, else "Still stopping" (the end is read once reap() joins it).
  void leaveView(bool joined);
  // Entry `index`'s name: the chart's, or the file's.
  const char* entryName(size_t index) const;

  // ---- sweep task: the runner's client --------------------------------------------------------------------------
  // Every hook reaches the runner through `sweep`, the one it is handed, never through `runner` (null once detached).
  // A run's start or the next picture: the picture from Painting, painted from the entry.
  bool onReplay(GrayRunner& sweep, GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset) override;
  // A resume that finds the picture held: the run holds again (defensive; see the definition).
  bool keepHolding(GrayRunner& sweep) override;
  // The session cap's clean: the hold in progress dropped; the next slot paints the entry again.
  bool onCapClean(GrayRunner& sweep) override;
  // Nothing moves; after a clean (`stale`) the entry is painted again.
  bool prepareSlot(GrayRunner& sweep, GameDisplayLab::GrayModel& model, uint32_t prevMs, uint32_t nowMs, bool stale,
                   const GameDisplayLab::GrayPreset& preset) override;
  // GrayPicture::slotDone decides: the paint settled (logged with the per-level counts, the marker out), the marker
  // settled (the run holds).
  SlotVerdict afterSlot(GrayRunner& sweep, const GameDisplayLab::SlotResult& slot, uint32_t slotStartUs,
                        uint32_t periodUs, uint32_t elapsedMs) override;
  // The current entry into the model: the chart, or the file read through readGrayImage; false when the file could
  // not be read (the runner's client code says why) or the read was stopped. A file's path and name are built in the
  // runner's client text, not on the task's stack, and after the read only it, the runner and locals are touched: a
  // read that outlasts endLocked's deadline returns to a runner the screen has given up.
  bool paintEntry(GrayRunner& sweep, GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset);

  DisplayLabActivity& lab;
  // Why the screen cannot run (its one row), or null.
  const char* notice = nullptr;
  // The Status row's line.
  const char* status = nullptr;
  char imageText[NAME_BYTES + 24] = {};
  char levelsText[48] = {};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  // The view is up: the panel shows the drive and render() draws nothing. Written by the loop task, read by the render
  // task.
  std::atomic<bool> shownOnPanel{false};
  // The Dither row: the loop task's, read by the sweep task (only written while no task is up).
  GameDisplayLab::Dither dither = GameDisplayLab::Dither::Plain;
  // The listed files, sorted, over their names' heap array.
  std::unique_ptr<GameDisplayLab::ImageName[]> imageNames;
  GameDisplayLab::ImageList images;
  // The entry the next paint shows (0: the chart; i: the file i - 1): the loop task's, read by the sweep task only when
  // it serves a replay (a run's start, or Next from a hold), so the loop writes it before it asks for one.
  std::atomic<uint16_t> requested{0};
  // Two logical rows: the chart's grays, and the quantised shades of a row being painted (the sweep task's).
  std::unique_ptr<uint8_t[]> grayRow;
  std::unique_ptr<uint8_t[]> shadeRow;

  // The sweep task and its working set; this screen is its client.
  // Null when it had no room (the notice row), or once endLocked detached it.
  std::unique_ptr<GrayRunner> runner;

  // ---- the sweep task's own, while it is up (read by the loop task only once it is deleted) ------------------------
  GameDisplayLab::GrayPicture picture;
  // The entry the task is painting (requested, read at each replay).
  uint16_t painted = 0;
};

#endif  // CROSSHATCH_DISPLAY_LAB

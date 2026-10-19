#include "GrayRunner.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include "activities/RenderLock.h"

using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::MaskPlanes;
using GameDisplayLab::Shade;

namespace {

// The client a detached runner answers to (endLocked gave up on its task, and the screen that was its client is
// gone): every hook is answered here with GameDisplayLab::InertAnswers, which change nothing about how the run ends
// (the stop asked before every detach ends it, Stopped), and nothing of the screen is touched.
class DetachedClient final : public GrayRunner::Client {
 public:
  using Answers = GameDisplayLab::InertAnswers;
  bool onReplay(GrayRunner&, GrayModel&, const GrayPreset&) override { return Answers::replayPainted; }
  void onPaused(GrayRunner&) override {}
  bool keepHolding(GrayRunner&) override { return Answers::keepHolding; }
  void onResume(GrayRunner&, GrayModel&, const GrayPreset&) override {}
  bool onCapClean(GrayRunner&) override { return Answers::capCleanGoesOn; }
  bool prepareSlot(GrayRunner&, GrayModel&, uint32_t, uint32_t, bool, const GrayPreset&) override {
    return Answers::slotPrepared;
  }
  const GrayPreset& slotPreset(GrayRunner&, const GrayPreset& preset) override { return preset; }
  SlotVerdict afterSlot(GrayRunner&, const GameDisplayLab::SlotResult&, uint32_t, uint32_t, uint32_t) override {
    SlotVerdict verdict;
    verdict.reapply = Answers::slotReapplies;
    verdict.finished = Answers::slotFinishesRun;
    return verdict;
  }
  bool clockStill(const GrayRunner&) const override { return Answers::clockStill; }
};
DetachedClient detachedClient;

// The reap registry: runners whose task outlived endLocked's bound. Each such task frees its runner (the model, the
// masks, every buffer it could still touch) itself when it ends, and counts itself out here; while any is counted, no
// screen starts a run. A task that never returns is never counted out: its buffers stay allocated until a restart, by
// design (freeing them under it would be a use after free), and the screens ask for one.
std::atomic<int> detachedRunners{0};

}  // namespace

GrayRunner::GrayRunner(GfxRenderer& renderer, DisplayLabActivity& lab, Client& client, const char* who,
                       const uint32_t stackBytes)
    : renderer(renderer), lab(lab), client_(&client), who(who), stackBytes_(stackBytes) {}

int GrayRunner::detachedTasks() { return detachedRunners.load(); }

bool GrayRunner::allocate(const int logicalW, const int logicalH) {
  const MaskPlanes& drive = lab.masks();
  ScanBackend* backend = lab.backend();
  // The lab opens the screens only with a session; without one there is nothing to drive.
  if (!drive.valid() || !backend) {
    LOG_ERR("DLAB", "%s: no lab session", who);
    return false;
  }
  const uint16_t width = drive.width();
  const uint16_t height = drive.height();
  if (static_cast<size_t>(logicalW) * static_cast<size_t>(logicalH) != GrayModel::pixelCount(width, height)) {
    LOG_ERR("DLAB", "screen %dx%d does not cover the %ux%u panel", logicalW, logicalH, static_cast<unsigned>(width),
            static_cast<unsigned>(height));
    return false;
  }
  // The working set of one visit, never per slot: the model (3 B a pixel) and the follow-up mask pair, PSRAM only
  // (allocatePsram never falls back to internal RAM).
  const size_t modelBytes = GrayModel::bytesFor(width, height);
  const size_t maskBytes = MaskPlanes::bytesFor(width, height);
  pixelBuffer = HalMemory::allocatePsram(modelBytes);
  followBlack = HalMemory::allocatePsram(maskBytes);
  followWhite = HalMemory::allocatePsram(maskBytes);
  if (!pixelBuffer || !followBlack || !followWhite) {
    LOG_ERR("DLAB", "OOM: %u byte PSRAM model and two %u byte follow-up masks", static_cast<unsigned>(modelBytes),
            static_cast<unsigned>(maskBytes));
    return false;
  }
  const GrayPreset& preset = lab.preset();
  // History (3 B a pixel) only when the preset turns it on.
  if (preset.historyMode != 0) {
    const size_t historyBytes = GrayModel::historyBytesFor(width, height);
    historyBuffer = HalMemory::allocatePsram(historyBytes);
    if (!historyBuffer) {
      LOG_ERR("DLAB", "OOM: %u byte PSRAM gray history", static_cast<unsigned>(historyBytes));
      return false;
    }
  }
  // The weak bitmap (1 bit a pixel) only when weak mode is on and the backend can drive a weak row.
  weakRow_ = preset.weakWhiteToLight && backend->supportsWeakRow();
  if (preset.weakWhiteToLight && !weakRow_) LOG_INF("DLAB", "weak mode on, but %s has no weak row", backend->name());
  if (weakRow_) {
    const size_t weakBytes = GrayModel::weakBytesFor(width, height);
    weakBuffer = HalMemory::allocatePsram(weakBytes);
    if (!weakBuffer) {
      LOG_ERR("DLAB", "OOM: %u byte PSRAM weak bitmap", static_cast<unsigned>(weakBytes));
      return false;
    }
  }
  model_ = GrayModel(pixelBuffer.get(), width, height, historyBuffer.get(), weakBuffer.get());
  follow = MaskPlanes(followBlack.get(), followWhite.get(), width, height);
  model_.reset(Shade::W);
  LOG_INF("DLAB", "%s: %u B model, %u B follow-up masks, history %s, weak row %s", who,
          static_cast<unsigned>(modelBytes), static_cast<unsigned>(2 * maskBytes), historyBuffer ? "on" : "off",
          weakRow_ ? "on" : "off");
  return true;
}

void GrayRunner::release() {
  // The task owns the model and the masks while it is up: freeing them under it would be a use after free.
  if (sweepTask) {
    LOG_ERR("DLAB", "%s: release refused, the sweep task is up", who);
    return;
  }
  model_ = GrayModel();
  follow = MaskPlanes();
  weakRow_ = false;
  pixelBuffer.reset();
  followBlack.reset();
  followWhite.reset();
  historyBuffer.reset();
  weakBuffer.reset();
}

// ---- loop task ------------------------------------------------------------------------------------------------------

void GrayRunner::prepare(const bool pressesWaitForFirstPaint) {
  // The old startRun's guard (one task at a time): the fields below are the live task's while it is up.
  if (sweepTask) {
    LOG_ERR("DLAB", "%s: prepare refused, the sweep task is up", who);
    return;
  }
  // The task's fields, written before it is created (its creation orders them before its first read). reset() also
  // clears the hold (Held) and a pending next picture (Replaying).
  taskState.store(GameDisplayLab::TaskState::Running);
  stops.reset();
  sweepEnd = End::Stopped;
  failedScan_ = DisplayLabActivity::Scan::Done;
  sweeping = false;
  sceneStale = false;
  elapsedUs = 0;
  sceneMs = 0;
  leastScanHeadroom = UINT32_MAX;
  // A run that ends before a report over delivered scans keeps the Speed line as it was.
  statsReported_ = false;
  reportedTenths_ = 0;
  reportedMsPerScan_ = 0;
  scheduler.stop();
  control.reset(micros(), pressesWaitForFirstPaint);
}

bool GrayRunner::launch() {
  if (sweepTask) {
    LOG_ERR("DLAB", "%s: launch refused, the sweep task is up", who);
    return false;
  }
  const BaseType_t created = xTaskCreatePinnedToCore(&GrayRunner::sweepEntry, "DlabSweep", stackBytes_, this,
                                                     SWEEP_PRIORITY, &sweepTask, SWEEP_CORE);
  if (created == pdPASS && sweepTask) return true;
  sweepTask = nullptr;
  LOG_ERR("DLAB", "Cannot create the sweep task (%u byte stack)", static_cast<unsigned>(stackBytes_));
  return false;
}

GrayRunner::Press GrayRunner::press(const bool nextFromHold) {
  if (!sweepTask) return Press::Ignored;
  // One compare-exchange on the request word (SweepControl::press): a hold landing meanwhile cannot drop the press.
  const Press pressed = control.press(nextFromHold);
  if (pressed != Press::Ignored) notifySweep();
  return pressed;
}

bool GrayRunner::reap() {
  // A task that outlived stop()'s bound and has since ended: joined now (it is parked, holding nothing).
  if (!sweepTask || !taskEnded()) return false;
  join();
  LOG_INF("DLAB", "%s: the late sweep task ended and was joined", who);
  return true;
}

void GrayRunner::join() {
  vTaskDelete(sweepTask);  // parked in vTaskSuspend
  sweepTask = nullptr;
  // A cover the view saw while the task was still stopping: its end is read only now.
  if (stops.joined() && sweepEnd == End::Stopped) sweepEnd = End::Covered;
}

void GrayRunner::askStop() {
  control.requestStop();
  notifySweep();
  stops.ask(millis());
}

bool GrayRunner::preventAutoSleep() const {
  // Run, or a hold within HOLD_AWAKE_MS of its start, and never once a stop is asked (a task still ending, or
  // detached, must not hold the device up; a forgotten hold must not keep the CPU up for good: auto-sleep then ends the
  // run as any leave does), from one load of the word (SweepControl::keepsAwake).
  return sweepTask != nullptr && control.keepsAwake(millis(), heldSinceMs.load(), HOLD_AWAKE_MS);
}

bool GrayRunner::viewEnds(const uint32_t nowUs) {
  // Another screen pushed over the view (the light panel's top-edge swipe) gets no onExit, stops the loop, and draws
  // over the drive; a gap that long means it happened (the task has refused its scans since), and the view ends. The
  // view also ends when the task ended on its own (a failed scan, the cover, the client's end), or was ended by
  // prepareForSleep.
  const bool covered = control.heartbeatAgeUs(nowUs) > COVERED_GAP_US;
  if (covered) LOG_INF("DLAB", "gray sweep: covered by another screen");
  if (!covered && sweepTask && !taskEnded()) return false;
  const bool joined = stop();
  // A cover the loop saw first (the task paused, or ended by the stop above) reads as one too; while the task is still
  // stopping its end is its own, so the cover waits for join().
  if (covered && stops.coverSeen(joined) && sweepEnd == End::Stopped) sweepEnd = End::Covered;
  return true;
}

void GrayRunner::notifySweep() {
  // The task is deleted only by the screen through this runner, so a live handle is a live task (it may be parked
  // after its end).
  if (sweepTask) xTaskNotifyGive(sweepTask);
}

bool GrayRunner::stop() {
  if (!sweepTask) return true;
  askStop();
  // Normally ends after the scan, the clean or the client's paint in flight (the run's first clean or the session cap's
  // is one FULL refresh; a scan is bounded by DisplayLabActivity::SCAN_TIMEOUT_MS, the SDK's own BUSY limit beyond it;
  // a decode checks stopRequested before every output row, and its reads give nothing once it is asked). Waited for
  // until STOP_WAIT_MS after the first stop asked: a task still running then (a decoder that never returns to its
  // callback) is not deleted, since it may hold the Storage mutex, and stays the runner's (running() true, so no new
  // run starts; it refuses any further scan, the stop being sticky) until it ends and reap() or a later stop joins it.
  while (!taskEnded()) {
    if (stops.expired(millis(), STOP_WAIT_MS)) {
      if (stops.giveUp()) {
        LOG_ERR("DLAB", "%s: the sweep task did not end within %lu ms of the stop; left running, not deleted", who,
                static_cast<unsigned long>(STOP_WAIT_MS));
      }
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  join();
  return true;
}

void GrayRunner::endLocked(std::unique_ptr<GrayRunner>& owned) {
  if (!owned || !owned->sweepTask) return;
  owned->endLockedOrDetach(owned);
}

void GrayRunner::endLockedOrDetach(std::unique_ptr<GrayRunner>& owned) {
  askStop();
  while (!taskEnded()) {
    // Inside the panel the task blocks without the render mutex only while taking it (in DisplayLabActivity::scan or
    // cleanToWhite): blocked there, it waits for the mutex this caller holds and holds nothing, so it is deleted
    // where it waits (GameVM::abandon deletes a task it has seen safe the same way), before any detach: a detached task
    // must never enter the panel. The scan lambda checks DisplayLabActivity::scan's refusals (and logs them) before
    // entering the panel, so no log call, which can block on the USB serial's lock, runs inside it; the one window left
    // is the display being inverted between that check and scan()'s own, whose LOG_ERR then runs inside the panel. The
    // mutex wait cannot end while this caller holds the mutex, the task runs on SWEEP_CORE, the loop task's core, and
    // the sequence from the check to the delete makes no blocking call, so the task cannot run between them; the
    // logging comes after the delete.
    if (control.insidePanel() && eTaskGetState(sweepTask) == eBlocked) {
      const auto freeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(sweepTask));
      vTaskSuspend(sweepTask);
      vTaskDelete(sweepTask);
      sweepTask = nullptr;
      logStackEnd(freeBytes);
      LOG_INF("DLAB", "gray sweep: task deleted while it waited for the render lock");
      if (sweeping) logStats(micros());
      sweeping = false;
      sweepEnd = End::Stopped;
      return;
    }
    // Until STOP_WAIT_MS after the first stop asked (a stop that already gave up leaves none: detached at once), then
    // detached: Home and sleep never hang on a task that does not return. Outside the panel the task is never deleted.
    // The longest such wait is a client's paint (the gray viewer's decode): its SD reads take the Storage mutex inside
    // the decoder, and the reader checks the stop once per decoded row, so this caller spins, holding the render
    // mutex, for at most one row's decode and SD read. No deadlock: the task takes the Storage mutex but never the
    // render mutex while it decodes, and no holder of the Storage mutex waits on the render mutex (lock order: render,
    // then Storage, never the reverse).
    if (stops.expired(millis(), STOP_WAIT_MS)) {
      if (detach(owned)) return;  // nothing of this runner from here: the task may free it at any moment
      break;                      // it ended just now: joined below
    }
    vTaskDelay(1);
  }
  join();
}

bool GrayRunner::detach(std::unique_ptr<GrayRunner>& owned) {
  // Everything the detach needs is taken or set before the exchange: once it succeeds the task owns the runner and may
  // free it at once, so the log below names the screen from a local.
  const char* const name = who;
  // The inert client and the registry first, then the exchange (GameDisplayLab::detachTask). Lost: its own end won,
  // it calls no hook again, both are undone, and the screen keeps its runner and joins it.
  if (!GameDisplayLab::detachTask<Client>(taskState, detachedRunners, client_, &detachedClient)) return false;
  (void)owned.release();
  LOG_ERR("DLAB",
          "%s: the sweep task did not end within %lu ms of the stop; detached. It frees its runner's buffers when it "
          "ends; if it never does they are leaked by design until a restart, and no run starts before one",
          name, static_cast<unsigned long>(STOP_WAIT_MS));
  return true;
}

// ---- sweep task -----------------------------------------------------------------------------------------------------

void GrayRunner::sweepEntry(void* param) {
  GrayRunner* self = static_cast<GrayRunner*>(param);
  self->sweepRun();
  // Its end, published in one exchange against endLocked's detach. Done: the screen may delete this task at any time
  // from here, so touch nothing of it and wait to be deleted (a task that deleted itself could be notified after it
  // was freed). Detached: the screen gave the runner up and will neither notify nor delete this task, so the task
  // frees the runner (every buffer it could touch) and deletes itself.
  if (GameDisplayLab::finishTask(self->taskState)) {
    for (;;) vTaskSuspend(nullptr);
  }
  LOG_INF("DLAB", "%s: the detached sweep task ended; its buffers freed", self->who);
  delete self;
  detachedRunners.fetch_sub(1);
  vTaskDelete(nullptr);
}

void GrayRunner::sweepRun() {
  stackFloor = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
  while (true) {
    const GameDisplayLab::SweepControl::Requests requests = control.take();
    if (requests.stop) {
      sweepEnd = End::Stopped;
      break;
    }
    if (!serve(requests)) break;
    if (!sweeping) {
      // Paused: a control's notification wakes it.
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    const uint32_t nowUs = micros();
    if (!scheduler.due(nowUs)) {
      // Until the next slot, or a control's notification; at least one tick.
      const TickType_t ticks = pdMS_TO_TICKS((scheduler.nextUs() - nowUs) / 1000);
      ulTaskNotifyTake(pdTRUE, ticks > 0 ? ticks : 1);
      continue;
    }
    if (!runSlot(nowUs)) break;
  }
  if (sweeping) reportStats(micros());
  sweeping = false;
  control.releaseHold();
  scheduler.stop();
  logStackEnd(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
}

bool GrayRunner::serve(const GameDisplayLab::SweepControl::Requests& requests) {
  const bool cleaning = requests.replay || requests.clean;
  if (sweeping && (!requests.run || cleaning)) {
    // A pause, or a clean, which is a pause of the clock: the speed up to here, the clock stopped.
    const uint32_t nowUs = micros();
    advanceClock(nowUs);
    reportStats(nowUs);
    sweeping = false;
    scheduler.stop();
    if (!requests.run) {
      LOG_INF("DLAB", "gray sweep paused at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
      client().onPaused(*this);
    }
  }
  const GrayPreset& preset = lab.preset();
  // A replay with presses waiting always comes with Run: SweepControl::press sets it in the same exchange (and reset()
  // with it), and no press can clear it before this take (presses are ignored while it is pending), so the picture is
  // swept once painted.
  const bool run = requests.run;
  if (requests.replay) {
    // A run's clean before the paint is white, black, white (story dlab-g2, D14): after one the previous screen's text
    // ghosted through the painted bands.
    if (!cleanToWhite(CleanPasses::WhiteBlackWhite)) return false;
    // A replay from a hold (the viewer's next picture) ends the hold: the panel is white again.
    control.releaseHold();
    elapsedUs = 0;
    sceneMs = 0;
    const bool painted = client().onReplay(*this, model_, preset);
    // Presses count again once the picture is painted (or could not be).
    if (requests.replaying) control.replayServed();
    if (!painted) return paintFailed();
    if (stopSeen()) return false;
    sceneStale = false;
  } else if (requests.clean) {
    if (!cleanToWhite()) return false;  // the next slot reapplies the picture at the paused clock
    // The panel is white again: a hold the clean met is over (requestClean has no caller today).
    control.releaseHold();
    LOG_INF("DLAB", "gray sweep cleaned at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
  }
  if (run && !sweeping) {
    // A client whose picture stays held (the gray viewer's finished picture, which a press cannot resume) is asked
    // first: the run holds again, with no resume line, no slot clock started, and the hold's start (and its
    // HOLD_AWAKE_MS bound) unchanged.
    if (client().keepHolding(*this)) {
      control.holdRun();
      return true;
    }
    // The task, not the press, ends a hold: a resume then a pause before this take kept it.
    control.releaseHold();
    const uint32_t nowUs = micros();
    clockUs = nowUs;
    statsSinceUs = nowUs;
    scheduler.start(nowUs);
    sweeping = true;
    if (!requests.replay) LOG_INF("DLAB", "gray sweep resumed at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
    // A run held at a photo spot: the client drives its marker back before anything else moves (it does nothing
    // unless it holds).
    client().onResume(*this, model_, preset);
  }
  return true;
}

bool GrayRunner::stopSeen() {
  if (!control.stopRequested()) return false;
  sweepEnd = End::Stopped;
  return true;
}

bool GrayRunner::paintFailed() {
  sweepEnd = control.stopRequested() ? End::Stopped : End::PaintFailed;
  return false;
}

bool GrayRunner::admitted(const GameDisplayLab::SweepControl::Gate gate) {
  switch (gate) {
    case GameDisplayLab::SweepControl::Gate::Enter:
      return true;
    case GameDisplayLab::SweepControl::Gate::Stop:
      sweepEnd = End::Stopped;
      return false;
    case GameDisplayLab::SweepControl::Gate::Covered:
      LOG_INF("DLAB", "gray sweep: loop silent for over %lu ms, a screen covers the view; ending",
              static_cast<unsigned long>(COVERED_GAP_US / 1000));
      sweepEnd = End::Covered;
      return false;
  }
  return false;
}

void GrayRunner::cleanWhiteBlackWhite(GfxRenderer& renderer, DisplayLabActivity& lab) {
  // The lab's clean sends the framebuffer with a FULL refresh: each pass is the framebuffer filled, then a clean.
  for (const uint8_t fill : {uint8_t{0xFF}, uint8_t{0x00}, uint8_t{0xFF}}) {
    renderer.clearScreen(fill);
    lab.cleanNow();
  }
}

bool GrayRunner::cleanToWhite(const CleanPasses passes) {
  if (!admitted(control.enterPanel(micros(), COVERED_GAP_US))) return false;
  {
    // White last, so the panel and the model agree.
    RenderLock lock;
    if (passes == CleanPasses::WhiteBlackWhite) {
      cleanWhiteBlackWhite(renderer, lab);
    } else {
      renderer.clearScreen(0xFF);
      lab.cleanNow();
    }
  }
  if (passes == CleanPasses::WhiteBlackWhite) {
    LOG_INF("DLAB", "gray sweep: cleaned white, black, white before the paint");
  }
  control.leavePanel();
  model_.reset(Shade::W);
  sceneStale = true;
  return true;
}

void GrayRunner::advanceClock(const uint32_t nowUs) {
  if (sweeping && !client().clockStill(*this)) elapsedUs += nowUs - clockUs;
  clockUs = nowUs;
}

void GrayRunner::holdForPhoto() {
  const uint32_t nowUs = micros();
  advanceClock(nowUs);
  reportStats(nowUs);
  sweeping = false;
  scheduler.stop();
  // The hold's start first, then Held and Run in one step (preventAutoSleep reads both from one load).
  heldSinceMs.store(millis());
  control.holdRun();
}

bool GrayRunner::runSlot(const uint32_t nowUs) {
  if (stopSeen()) return false;
  advanceClock(nowUs);
  uint32_t slotStartUs = nowUs;
  // A slot is two scans; the lab cleans on its own at the session cap, which must not fall between them.
  if (lab.scansBeforeClean() < SCANS_PER_SLOT) {
    LOG_INF("DLAB", "gray sweep: session cap near, cleaning first");
    reportStats(nowUs);  // the speed up to the clean
    if (!cleanToWhite()) {
      sweeping = false;  // reported already: sweepRun must not report the empty window after it
      return false;
    }
    if (!client().onCapClean(*this)) {
      sweepEnd = End::Finished;
      sweeping = false;  // reported already: sweepRun must not report the empty window after it
      return false;
    }
    // The repaint after the clean (the transition test's reapply, the gray viewer's decode, seconds for a file) belongs
    // to the clean, not to the slot: done here, before the clock, the slot and the speed window restart, so it is
    // neither counted as skipped periods nor in the slot's measured length.
    const uint32_t cleanMs = static_cast<uint32_t>(elapsedUs / 1000);
    if (!client().prepareSlot(*this, model_, sceneMs, cleanMs, true, lab.preset())) {
      sweeping = false;  // reported already: sweepRun must not report the empty window after it
      return paintFailed();
    }
    if (stopSeen()) {
      sweeping = false;
      return false;
    }
    sceneStale = false;
    sceneMs = cleanMs;
    // The clean's FULL refresh is a pause of the sweep: the clock, the slot and the speed window restart after it.
    slotStartUs = micros();
    clockUs = slotStartUs;
    scheduler.resetWindow(slotStartUs);
    statsSinceUs = slotStartUs;
  }
  const uint32_t ms = static_cast<uint32_t>(elapsedUs / 1000);
  const GrayPreset& preset = lab.preset();
  if (!client().prepareSlot(*this, model_, sceneMs, ms, sceneStale, preset)) return paintFailed();
  // The paint may have been a decode the stop outlasted: nothing of the lab (its masks, below) from here.
  if (stopSeen()) return false;
  sceneStale = false;
  sceneMs = ms;

  DisplayLabActivity::Scan failure = DisplayLabActivity::Scan::Done;
  bool refused = false;
  // The delivered (Done) scans and their busy time, for the speed window.
  uint32_t doneScans = 0;
  uint32_t doneBusyUs = 0;
  const auto scan = [this, &preset, &failure, &refused, &doneScans, &doneBusyUs](const MaskPlanes& masks,
                                                                                 uint32_t& busyUs) {
    // A stop asked for ends the run as Stopped, whatever the refusal checks below would say.
    if (control.stopRequested()) {
      sweepEnd = End::Stopped;
      refused = true;
      return false;
    }
    // DisplayLabActivity::scan's refusals, checked (and logged) here, outside the panel: its own LOG_ERR runs before
    // its render lock, and a task deleted inside a log call would keep the USB serial's lock (endLocked).
    ScanBackend* backend = lab.backend();
    bool refusedHere = true;
    if (!backend || !lab.masks().valid()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: no lab session (backend or masks)");
    } else if (display.isInverted()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: display inverted");
    } else if (preset.rateIndex >= backend->rateCount()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: rate index %u of %u", static_cast<unsigned>(preset.rateIndex),
              static_cast<unsigned>(backend->rateCount()));
    } else {
      refusedHere = false;
    }
    if (refusedHere) {
      failure = DisplayLabActivity::Scan::Refused;
      return false;
    }
    // Stop or a cover cuts the slot here: the follow-up is skipped, and runGraySlot commits what was delivered.
    if (!admitted(control.enterPanel(micros(), COVERED_GAP_US))) {
      refused = true;
      return false;
    }
    // The room left to the scan's call chain (the backend and the SDK), for the run's stack log line.
    const uintptr_t here = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    const uint32_t headroom = here > stackFloor ? static_cast<uint32_t>(here - stackFloor) : 0;
    if (headroom < leastScanHeadroom) leastScanHeadroom = headroom;
    // One dose: a one-frame activation of each driven pixel.
    const ScanRequest request{.blackMask = masks.black(),
                              .whiteMask = masks.white(),
                              .frames = 1,
                              .rate = preset.rateIndex,
                              .swapPolarity = preset.swapPolarity};
    const DisplayLabActivity::Scan result = lab.scan(request, busyUs);
    control.leavePanel();
    if (result != DisplayLabActivity::Scan::Done) {
      failure = result;
      return false;
    }
    ++doneScans;
    doneBusyUs += busyUs;
    return true;
  };
  // The client's preset for this slot (the Ladder's held rhythm while it pulses; the preset itself otherwise).
  const GrayPreset& slotPreset = client().slotPreset(*this, preset);
  const GameDisplayLab::SlotResult slot =
      GameDisplayLab::runGraySlot(model_, lab.masks(), follow, slotPreset, weakRow_, scan);
  const uint32_t endUs = micros();
  scheduler.slotDone(slotStartUs, endUs, slot.busyUs);
  // Only delivered scans and their busy time count: a refused scan was never sent, and a TimedOut or Dropped scan's
  // time is no measurement.
  scheduler.record(doneScans, doneBusyUs);
  if (refused) return false;
  if (failure != DisplayLabActivity::Scan::Done) {
    // The lab logged why (and cleaned the panel after a timeout); the run ends here and the rows say how.
    failedScan_ = failure;
    sweepEnd = End::ScanFailed;
    return false;
  }
  // The slot's real length, from before the picture's update to after the commit: the scans plus the CPU work over
  // every pixel, which the scheduler's period (busy time + MARGIN_US) leaves out.
  const uint32_t periodUs = scheduler.periodUs() > endUs - slotStartUs ? scheduler.periodUs() : endUs - slotStartUs;
  const Client::SlotVerdict verdict = client().afterSlot(*this, slot, slotStartUs, periodUs, ms);
  // A reapply retargets every pixel at the next slot (a Static flip).
  if (verdict.reapply) sceneStale = true;
  if (verdict.finished) {
    sweepEnd = End::Finished;
    return false;
  }
  // A hold in afterSlot reported (and restarted the window) after endUs: a signed difference, so that window is not
  // reported again, empty, as a wrapped unsigned one was (a `gray scans_s=0.0` line after every hold).
  if (sweeping && static_cast<int32_t>(endUs - statsSinceUs) >= static_cast<int32_t>(STATS_EVERY_US)) {
    reportStats(endUs);
  }
  return true;
}

void GrayRunner::logStackEnd(const uint32_t freeBytes) const {
  LOG_INF("DLAB", "gray sweep task ended: stack %u bytes, high-water %u bytes free, least before a scan %u bytes",
          static_cast<unsigned>(stackBytes_), static_cast<unsigned>(freeBytes),
          static_cast<unsigned>(leastScanHeadroom == UINT32_MAX ? 0 : leastScanHeadroom));
  if (freeBytes < SWEEP_STACK_MARGIN_BYTES) {
    LOG_ERR("DLAB", "gray sweep task stack: %u bytes free, under the %u byte margin", static_cast<unsigned>(freeBytes),
            static_cast<unsigned>(SWEEP_STACK_MARGIN_BYTES));
  }
}

void GrayRunner::logStats(const uint32_t nowUs) const {
  LOG_INF("DLAB", "gray scans_s=%u.%u ms_per_scan=%u skipped=%lu",
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) / 10),
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) % 10),
          static_cast<unsigned>(scheduler.msPerScan()), static_cast<unsigned long>(scheduler.skipped()));
}

void GrayRunner::reportStats(const uint32_t nowUs) {
  logStats(nowUs);
  // The loop task writes the Speed line from these once the run is over, under the render lock. A window without a
  // delivered scan (a report just after the last one, or before a refused clean) keeps the last real speed.
  if (scheduler.windowScans() > 0) {
    reportedTenths_ = scheduler.scansPerSecondTenths(nowUs);
    reportedMsPerScan_ = scheduler.msPerScan();
    statsReported_ = true;
  }
  scheduler.resetWindow(nowUs);
  statsSinceUs = nowUs;
}

#endif  // CROSSHATCH_DISPLAY_LAB

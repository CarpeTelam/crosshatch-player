#include "TransitionTestActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <GraySlot.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::MaskPlanes;
using GameDisplayLab::PanelTurn;
using GameDisplayLab::Shade;

namespace {

// GfxRenderer's four orientations and the scene's panel turns name the same rotations in the same order, so the
// renderer's orientation is the scene's turn by value; the asserts pin each pair by name.
static_assert(static_cast<int>(PanelTurn::Portrait) == GfxRenderer::Portrait, "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::LandscapeClockwise) == GfxRenderer::LandscapeClockwise,
              "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::PortraitInverted) == GfxRenderer::PortraitInverted,
              "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::LandscapeCounterClockwise) == GfxRenderer::LandscapeCounterClockwise,
              "PanelTurn follows GfxRenderer");

PanelTurn turnOf(const GfxRenderer::Orientation orientation) { return static_cast<PanelTurn>(orientation); }

}  // namespace

TransitionTestActivity::TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* TransitionTestActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TRANSITION_TEST); }

void TransitionTestActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  shownOnPanel = false;
  notice = nullptr;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  snprintf(statsText, sizeof(statsText), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  if (!allocate()) {
    release();
    notice = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  buildRows();
}

bool TransitionTestActivity::allocate() {
  const MaskPlanes& drive = lab.masks();
  ScanBackend* backend = lab.backend();
  // The lab opens this screen only with a session; without one there is nothing to drive.
  if (!drive.valid() || !backend) {
    LOG_ERR("DLAB", "transition test: no lab session");
    return false;
  }
  const uint16_t width = drive.width();
  const uint16_t height = drive.height();
  const int logicalW = renderer.getScreenWidth();
  const int logicalH = renderer.getScreenHeight();
  if (static_cast<size_t>(logicalW) * static_cast<size_t>(logicalH) != GrayModel::pixelCount(width, height)) {
    LOG_ERR("DLAB", "screen %dx%d does not cover the %ux%u panel", logicalW, logicalH, static_cast<unsigned>(width),
            static_cast<unsigned>(height));
    return false;
  }
  // The working set of one visit, never per slot: the model (2 B a pixel) and the follow-up mask pair, PSRAM only
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
  weakRow = preset.weakWhiteToLight && backend->supportsWeakRow();
  if (preset.weakWhiteToLight && !weakRow) LOG_INF("DLAB", "weak mode on, but %s has no weak row", backend->name());
  if (weakRow) {
    const size_t weakBytes = GrayModel::weakBytesFor(width, height);
    weakBuffer = HalMemory::allocatePsram(weakBytes);
    if (!weakBuffer) {
      LOG_ERR("DLAB", "OOM: %u byte PSRAM weak bitmap", static_cast<unsigned>(weakBytes));
      return false;
    }
  }
  model = GrayModel(pixelBuffer.get(), width, height, historyBuffer.get(), weakBuffer.get());
  follow = MaskPlanes(followBlack.get(), followWhite.get(), width, height);
  scene = GameDisplayLab::TransitionScene(turnOf(renderer.getOrientation()), logicalW, logicalH, width, height);
  model.reset(Shade::W);
  LOG_INF("DLAB", "transition test: %u B model, %u B follow-up masks, history %s, weak row %s",
          static_cast<unsigned>(modelBytes), static_cast<unsigned>(2 * maskBytes), historyBuffer ? "on" : "off",
          weakRow ? "on" : "off");
  return true;
}

void TransitionTestActivity::release() {
  model = GrayModel();
  follow = MaskPlanes();
  weakRow = false;
  pixelBuffer.reset();
  followBlack.reset();
  followWhite.reset();
  historyBuffer.reset();
  weakBuffer.reset();
}

void TransitionTestActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep, which also drains a covered
  // screen from the stack): take no RenderLock (12cc816), and end the task without waiting on that mutex.
  endSweepLocked();
  if (shownOnPanel.load()) {
    // The next screen's first frame cleans the panel; the lab's own exit cleans it too when it is left.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  release();
  Activity::onExit();
}

void TransitionTestActivity::prepareForSleep() {
  // ActivityManager::prepareForSleep holds the render mutex, for this screen as the current one or as a covered one.
  endSweepLocked();
}

bool TransitionTestActivity::handleHomeGesture() {
  // Called from ActivityManager::loop without the render mutex, before goHome's Replace runs onExit.
  stopSweep();
  return false;
}

void TransitionTestActivity::buildRows() {
  if (notice) {
    fui::ListItem item;
    item.label = notice;
    item.actionValue = 0;
    rowItems[0] = item;
    return;
  }
  const auto set = [this](const Row row, const char* label, const char* subtitle) {
    fui::ListItem item;
    item.label = label;
    item.subtitle = subtitle;
    item.actionValue = static_cast<int16_t>(row);
    rowItems[static_cast<size_t>(row)] = item;
  };
  set(Row::Run, tr(STR_GAMES_DLAB_RUN), status);
  set(Row::Stats, tr(STR_GAMES_DLAB_SPEED), statsText);
  set(Row::Controls, tr(STR_GAMES_DLAB_CONTROLS), tr(STR_GAMES_DLAB_TRANSITION_CONTROLS));
}

void TransitionTestActivity::render(RenderLock&& lock) {
  // While the test view is up the panel shows only what the scans drive (D4): a routed tap or a swipe requests a
  // repaint on its own, and none may run. Leaving the view clears the flag and asks again, with a FULL refresh.
  if (shownOnPanel.load()) return;
  UiListActivity::render(std::move(lock));
}

void TransitionTestActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 3;
  syncListViewport(screen, props);
  screen.list(props);
}

void TransitionTestActivity::activateIndex(const int index) {
  if (shownOnPanel.load()) {
    // A tap over the rows' area (the only taps the list routes) pauses or resumes.
    app.clearTapFlash();
    togglePause();
    return;
  }
  if (notice || index != static_cast<int>(Row::Run)) return;
  startRun();
}

bool TransitionTestActivity::handleCustomInput() {
  if (!shownOnPanel.load()) return false;
  // A swipe would scroll the hidden list: consumed while the test view is up.
  if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) return true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    stopSweep();
    leaveView(endText());
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    togglePause();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
    control.requestReplay();
    notifySweep();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    control.requestClean();
    notifySweep();
    return true;
  }
  return false;  // a tap over the rows' area reaches activateIndex through the base's touch routing
}

void TransitionTestActivity::navigateButtons() {
  if (shownOnPanel.load()) return;
  UiListActivity::navigateButtons();
}

void TransitionTestActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; leaving the lab cleans the panel
}

// ---- loop task ----------------------------------------------------------------------------------------------------

void TransitionTestActivity::loop() {
  if (shownOnPanel.load()) {
    // Another screen pushed over this one (the light panel's top-edge swipe) gets no onExit here, stops this loop, and
    // draws over the drive; a gap that long means it happened (the task has refused its scans since), and the view
    // ends with the rows' FULL refresh. The view also ends when the task ended on its own (a failed scan, the cover),
    // or was ended by prepareForSleep.
    const bool covered = control.heartbeatAgeUs(micros()) > COVERED_GAP_US;
    if (covered) LOG_INF("DLAB", "gray sweep: covered by another screen");
    if (covered || !sweepTask || sweepDone.load(std::memory_order_acquire)) {
      stopSweep();
      leaveView(endText());
      // This pass's input belonged to the test view: a Back or Confirm release here must not pop the screen or
      // start a run. The next pass serves the rows.
      control.heartbeat(micros());
      return;
    }
  }
  UiListActivity::loop();
  // The sweep task refuses its next scan once this is older than COVERED_GAP_US.
  control.heartbeat(micros());
}

void TransitionTestActivity::startRun() {
  if (sweepTask) return;
  app.clearTapFlash();
  // The task's fields, written before it is created (its creation orders them before its first read).
  sweepDone.store(false);
  sweepEnd = SweepEnd::Stopped;
  failedScan = DisplayLabActivity::Scan::Done;
  sweeping = false;
  sceneStale = false;
  elapsedUs = 0;
  sceneMs = 0;
  leastScanHeadroom = UINT32_MAX;
  // A run that ends before its first report shows a 0.0 speed, not the last run's.
  statsReported = true;
  reportedTenths = 0;
  reportedMsPerScan = 0;
  scheduler.stop();
  control.reset(micros());
  {
    RenderLock lock(*this);
    shownOnPanel = true;  // from here the render task draws nothing; no task is alive yet
  }
  const BaseType_t created = xTaskCreatePinnedToCore(&TransitionTestActivity::sweepEntry, "DlabSweep",
                                                     SWEEP_STACK_BYTES, this, SWEEP_PRIORITY, &sweepTask, SWEEP_CORE);
  if (created == pdPASS && sweepTask) return;
  sweepTask = nullptr;
  LOG_ERR("DLAB", "Cannot create the sweep task (%u byte stack)", static_cast<unsigned>(SWEEP_STACK_BYTES));
  {
    RenderLock lock(*this);
    shownOnPanel = false;  // nothing was sent: the rows stay as they are, with the reason
    status = tr(STR_GAMES_DLAB_NO_TASK);
    buildRows();
  }
  requestUpdate();
}

void TransitionTestActivity::togglePause() {
  if (!sweepTask) return;
  control.requestRun(!control.runRequested());
  notifySweep();
}

void TransitionTestActivity::notifySweep() {
  // The task is deleted only by this screen, so a live handle is a live task (it may be parked after its end).
  if (sweepTask) xTaskNotifyGive(sweepTask);
}

void TransitionTestActivity::stopSweep() {
  if (!sweepTask) return;
  control.requestStop();
  notifySweep();
  // Bounded by the scan or the clean in flight (a replay's, NavPrevious's or the session cap's clean is one FULL
  // refresh; a scan is bounded by DisplayLabActivity::SCAN_TIMEOUT_MS, the SDK's own BUSY limit beyond it), as the
  // inline scans were.
  while (!sweepDone.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  vTaskDelete(sweepTask);  // parked in vTaskSuspend
  sweepTask = nullptr;
}

void TransitionTestActivity::endSweepLocked() {
  if (!sweepTask) return;
  control.requestStop();
  notifySweep();
  while (!sweepDone.load(std::memory_order_acquire)) {
    // Inside the panel the task blocks without the render mutex only while taking it (in DisplayLabActivity::scan or
    // cleanToWhite): blocked there, it waits for the mutex this caller holds and holds nothing, so it is deleted
    // where it waits (GameVM::abandon deletes a task it has seen safe the same way). The scan lambda checks
    // DisplayLabActivity::scan's refusals (and logs them) before entering the panel, so no log call, which can block
    // on the USB serial's lock, runs inside it; the one window left is the display being inverted between that
    // check and scan()'s own, whose LOG_ERR then runs inside the panel. The mutex wait cannot end while this caller
    // holds the mutex, and the task runs on SWEEP_CORE, the loop task's core, where this task is the one running,
    // so it cannot move between the check and the delete.
    if (control.insidePanel() && eTaskGetState(sweepTask) == eBlocked) {
      LOG_INF("DLAB", "gray sweep task ended: stack %u bytes, high-water %u bytes free, least before a scan %u bytes",
              static_cast<unsigned>(SWEEP_STACK_BYTES), static_cast<unsigned>(uxTaskGetStackHighWaterMark(sweepTask)),
              static_cast<unsigned>(leastScanHeadroom == UINT32_MAX ? 0 : leastScanHeadroom));
      vTaskDelete(sweepTask);
      sweepTask = nullptr;
      LOG_INF("DLAB", "gray sweep: task deleted while it waited for the render lock");
      if (sweeping) logStats(micros());
      sweeping = false;
      sweepEnd = SweepEnd::Stopped;
      return;
    }
    vTaskDelay(1);
  }
  vTaskDelete(sweepTask);  // parked in vTaskSuspend
  sweepTask = nullptr;
}

const char* TransitionTestActivity::endText() const {
  switch (sweepEnd) {
    case SweepEnd::Stopped:
    case SweepEnd::Covered:
      return tr(STR_GAMES_DLAB_STOPPED);
    case SweepEnd::ScanFailed:
      return DisplayLabActivity::outcomeText(failedScan);
  }
  return tr(STR_GAMES_DLAB_STOPPED);
}

void TransitionTestActivity::leaveView(const char* outcome) {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    if (statsReported) {
      snprintf(statsText, sizeof(statsText), tr(STR_GAMES_DLAB_STATS), static_cast<unsigned>(reportedTenths / 10),
               static_cast<unsigned>(reportedTenths % 10), static_cast<unsigned>(reportedMsPerScan));
    }
    status = outcome;
    rowItems[static_cast<size_t>(Row::Run)].subtitle = status;
    rowItems[static_cast<size_t>(Row::Stats)].subtitle = statsText;
    // The rows come back over the drive with a FULL refresh, which cleans the panel; promoted before the flag
    // clears, so no repaint can slip in without it.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  LOG_INF("DLAB", "gray sweep stopped: %s", outcome);
  requestUpdate();
}

// ---- sweep task ---------------------------------------------------------------------------------------------------

void TransitionTestActivity::sweepEntry(void* param) {
  static_cast<TransitionTestActivity*>(param)->sweepRun();
  // sweepRun has published sweepDone, after which the screen may delete this task at any time: touch nothing of it,
  // and wait here to be deleted (a task that deleted itself could be notified after it was freed).
  for (;;) vTaskSuspend(nullptr);
}

void TransitionTestActivity::sweepRun() {
  stackFloor = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
  while (true) {
    const GameDisplayLab::SweepControl::Requests requests = control.take();
    if (requests.stop) {
      sweepEnd = SweepEnd::Stopped;
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
  scheduler.stop();
  const auto freeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  LOG_INF("DLAB", "gray sweep task ended: stack %u bytes, high-water %u bytes free, least before a scan %u bytes",
          static_cast<unsigned>(SWEEP_STACK_BYTES), static_cast<unsigned>(freeBytes),
          static_cast<unsigned>(leastScanHeadroom == UINT32_MAX ? 0 : leastScanHeadroom));
  if (freeBytes < SWEEP_STACK_MARGIN_BYTES) {
    LOG_ERR("DLAB", "gray sweep task stack: %u bytes free, under the %u byte margin", static_cast<unsigned>(freeBytes),
            static_cast<unsigned>(SWEEP_STACK_MARGIN_BYTES));
  }
  sweepDone.store(true, std::memory_order_release);
}

bool TransitionTestActivity::serve(const GameDisplayLab::SweepControl::Requests& requests) {
  const bool cleaning = requests.replay || requests.clean;
  if (sweeping && (!requests.run || cleaning)) {
    // A pause, or a clean, which is a pause of the clock: the speed up to here, the clock stopped.
    const uint32_t nowUs = micros();
    advanceClock(nowUs);
    reportStats(nowUs);
    sweeping = false;
    scheduler.stop();
    if (!requests.run) LOG_INF("DLAB", "gray sweep paused at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
  }
  const GrayPreset& preset = lab.preset();
  if (requests.replay) {
    if (!cleanToWhite()) return false;
    elapsedUs = 0;
    sceneMs = 0;
    scene.applyAll(model, 0, preset);
    sceneStale = false;
    LOG_INF("DLAB", "gray sweep: %u pixels to drive, rate %u, swap %u, retarget %u, history %u, weak row %u",
            static_cast<unsigned>(model.activeCount()), static_cast<unsigned>(preset.rateIndex),
            static_cast<unsigned>(preset.swapPolarity), static_cast<unsigned>(preset.retargetPercent),
            static_cast<unsigned>(historyBuffer ? preset.historyMode : 0), static_cast<unsigned>(weakRow));
  } else if (requests.clean) {
    if (!cleanToWhite()) return false;  // the next slot reapplies the scene at the paused clock
    LOG_INF("DLAB", "gray sweep cleaned at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
  }
  if (requests.run && !sweeping) {
    const uint32_t nowUs = micros();
    clockUs = nowUs;
    statsSinceUs = nowUs;
    scheduler.start(nowUs);
    sweeping = true;
    if (!requests.replay) LOG_INF("DLAB", "gray sweep resumed at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
  }
  return true;
}

bool TransitionTestActivity::admitted(const GameDisplayLab::SweepControl::Gate gate) {
  switch (gate) {
    case GameDisplayLab::SweepControl::Gate::Enter:
      return true;
    case GameDisplayLab::SweepControl::Gate::Stop:
      sweepEnd = SweepEnd::Stopped;
      return false;
    case GameDisplayLab::SweepControl::Gate::Covered:
      LOG_INF("DLAB", "gray sweep: loop silent for over %lu ms, a screen covers the view; ending",
              static_cast<unsigned long>(COVERED_GAP_US / 1000));
      sweepEnd = SweepEnd::Covered;
      return false;
  }
  return false;
}

bool TransitionTestActivity::cleanToWhite() {
  if (!admitted(control.enterPanel(micros(), COVERED_GAP_US))) return false;
  {
    // The lab's clean sends the framebuffer with a FULL refresh: white first, so the panel and the model agree.
    RenderLock lock(*this);
    renderer.clearScreen(0xFF);
    lab.cleanNow();
  }
  control.leavePanel();
  model.reset(Shade::W);
  sceneStale = true;
  return true;
}

void TransitionTestActivity::advanceClock(const uint32_t nowUs) {
  if (sweeping) elapsedUs += nowUs - clockUs;
  clockUs = nowUs;
}

bool TransitionTestActivity::runSlot(const uint32_t nowUs) {
  advanceClock(nowUs);
  uint32_t slotStartUs = nowUs;
  // A slot is two scans; the lab cleans on its own at the session cap, which must not fall between them.
  if (lab.scansBeforeClean() < SCANS_PER_SLOT) {
    LOG_INF("DLAB", "gray sweep: session cap near, cleaning first");
    reportStats(nowUs);  // the speed up to the clean
    if (!cleanToWhite()) return false;
    // The clean's FULL refresh is a pause of the sweep: the clock, the slot and the speed window restart after it.
    slotStartUs = micros();
    clockUs = slotStartUs;
    scheduler.resetWindow(slotStartUs);
    statsSinceUs = slotStartUs;
  }
  const uint32_t ms = static_cast<uint32_t>(elapsedUs / 1000);
  const GrayPreset& preset = lab.preset();
  if (sceneStale) {
    scene.applyAll(model, ms, preset);
    sceneStale = false;
  } else {
    scene.update(model, sceneMs, ms, preset);
  }
  sceneMs = ms;

  DisplayLabActivity::Scan failure = DisplayLabActivity::Scan::Done;
  bool refused = false;
  const auto scan = [this, &preset, &failure, &refused](const MaskPlanes& masks, uint32_t& busyUs) {
    // DisplayLabActivity::scan's refusals, checked (and logged) here, outside the panel: its own LOG_ERR runs before
    // its render lock, and a task deleted inside a log call would keep the USB serial's lock (endSweepLocked).
    ScanBackend* backend = lab.backend();
    if (!backend || !lab.masks().valid() || display.isInverted() || preset.rateIndex >= backend->rateCount()) {
      LOG_ERR("DLAB", "gray sweep: scan refused (no session, display inverted, or rate index %u out of range)",
              static_cast<unsigned>(preset.rateIndex));
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
    if (result != DisplayLabActivity::Scan::Done) failure = result;
    return result == DisplayLabActivity::Scan::Done;
  };
  const GameDisplayLab::SlotResult slot =
      GameDisplayLab::runGraySlot(model, lab.masks(), follow, preset, weakRow, scan);
  const uint32_t endUs = micros();
  scheduler.slotDone(slotStartUs, endUs, slot.busyUs);
  // Only delivered scans count: a scan the gate or the pre-check refused was never sent.
  scheduler.record((slot.driveDone ? 1u : 0u) + (slot.followDone ? 1u : 0u), slot.busyUs);
  if (refused) return false;
  if (failure != DisplayLabActivity::Scan::Done) {
    // The lab logged why (and cleaned the panel after a timeout); the run ends here and the rows say how.
    failedScan = failure;
    sweepEnd = SweepEnd::ScanFailed;
    return false;
  }
  if (endUs - statsSinceUs >= STATS_EVERY_US) reportStats(endUs);
  return true;
}

void TransitionTestActivity::logStats(const uint32_t nowUs) const {
  LOG_INF("DLAB", "gray scans_s=%u.%u ms_per_scan=%u skipped=%lu",
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) / 10),
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) % 10),
          static_cast<unsigned>(scheduler.msPerScan()), static_cast<unsigned long>(scheduler.skipped()));
}

void TransitionTestActivity::reportStats(const uint32_t nowUs) {
  logStats(nowUs);
  // The loop task writes the Speed line from these once the run is over (leaveView), under the render lock.
  reportedTenths = scheduler.scansPerSecondTenths(nowUs);
  reportedMsPerScan = scheduler.msPerScan();
  statsReported = true;
  scheduler.resetWindow(nowUs);
  statsSinceUs = nowUs;
}

#endif  // CROSSHATCH_DISPLAY_LAB

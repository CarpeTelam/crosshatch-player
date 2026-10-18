#include "TransitionTestActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <optional>
#include <utility>

#include "MappedInputManager.h"
#include "StockBenchGate.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::PanelTurn;
using GameDisplayLab::Shade;
using GameDisplayLab::SweepMode;

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

// The Mode row's line.
const char* modeText(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return tr(STR_GAMES_DLAB_MODE_STATIC);
    case SweepMode::Slow:
      return tr(STR_GAMES_DLAB_MODE_SLOW);
    case SweepMode::Fast:
      return tr(STR_GAMES_DLAB_MODE_FAST);
    case SweepMode::Ladder:
      return tr(STR_GAMES_DLAB_MODE_LADDER);
  }
  return "";
}

// The Hold row's line: On or Off, or that the Ladder always holds, whatever the row says (it keeps its value for the
// other modes).
const char* holdText(const bool on, const SweepMode mode) {
  if (mode == SweepMode::Ladder) return tr(STR_GAMES_DLAB_HOLD_LADDER);
  return on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
}

// The mode's name in the log.
const char* modeName(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return "static";
    case SweepMode::Slow:
      return "slow";
    case SweepMode::Fast:
      return "fast";
    case SweepMode::Ladder:
      return "ladder";
  }
  return "";
}

}  // namespace

TransitionTestActivity::TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput),
      lab(lab),
      runner(renderer, lab, *this, "transition test", SWEEP_STACK_BYTES) {}

const char* TransitionTestActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TRANSITION_TEST); }

void TransitionTestActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  shownOnPanel = false;
  notice = nullptr;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  snprintf(statsText, sizeof(statsText), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  // D12: Static on an UltraChip panel, whose slot is too long for the sweep; Fast stays the SSD1677's sweep.
  sweepMode = isUltraChip(display.getController()) ? SweepMode::Static : SweepMode::Fast;
  holdOn = true;  // D13: on every visit, a lab preference never saved with the preset
  const int logicalW = renderer.getScreenWidth();
  const int logicalH = renderer.getScreenHeight();
  if (runner.allocate(logicalW, logicalH)) {
    const GameDisplayLab::MaskPlanes& drive = lab.masks();
    scene = GameDisplayLab::TransitionScene(turnOf(renderer.getOrientation()), logicalW, logicalH, drive.width(),
                                            drive.height());
  } else {
    runner.release();
    notice = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  buildRows();
}

void TransitionTestActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep, which also drains a covered
  // screen from the stack): take no RenderLock (12cc816), and end the task without waiting on that mutex.
  runner.endLocked();
  if (shownOnPanel.load()) {
    // The next screen's first frame cleans the panel; the lab's own exit cleans it too when it is left.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  runner.release();
  Activity::onExit();
}

void TransitionTestActivity::prepareForSleep() {
  // ActivityManager::prepareForSleep holds the render mutex, for this screen as the current one or as a covered one.
  runner.endLocked();
}

bool TransitionTestActivity::handleHomeGesture() {
  // Called from ActivityManager::loop without the render mutex, before goHome's Replace runs onExit.
  runner.stop();
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
  set(Row::Mode, tr(STR_GAMES_DLAB_MODE), modeText(sweepMode));
  set(Row::Hold, tr(STR_GAMES_DLAB_HOLD_PHOTOS), holdText(holdOn, sweepMode));
  set(Row::Clean, tr(STR_GAMES_DLAB_CLEAN), tr(STR_GAMES_DLAB_CLEAN_DESC));
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
    // A tap over the rows' area (the only taps the list routes, where touch routes while the view is up) pauses or
    // resumes.
    app.clearTapFlash();
    runner.press(false);
    return;
  }
  if (notice) return;
  switch (static_cast<Row>(index)) {
    case Row::Run:
      startRun();  // a replay: the task cleans first
      return;
    case Row::Mode:
      stepMode();
      return;
    case Row::Hold:
      toggleHold();
      return;
    case Row::Clean:
      cleanFromRows();
      return;
    case Row::Stats:
    case Row::Controls:
      return;  // text only
  }
}

bool TransitionTestActivity::handleCustomInput() {
  if (!shownOnPanel.load()) return false;
  // The X4 Pro has no front buttons and its touch routing is dead while the view is up (D12), so the side buttons
  // carry the controls. Back first: the left-edge swipe is Back (wasBackGesture), and the swipe guard below would
  // swallow it. NavPrevious stops on its release, as Back does, so neither half of the press reaches the rows.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    runner.stop();
    leaveView(endText());
    return true;
  }
  // Any other swipe would scroll the hidden list: consumed while the test view is up.
  if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) return true;
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    runner.press(false);
    return true;
  }
  // A tap pauses only where touch routes while the view is up (it reaches activateIndex through the base's touch
  // routing); the X4 Pro's does not (D12), so its side buttons are the controls there.
  return false;
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
  // While the test view is up the view ends when another screen covered it (the light panel's top-edge swipe: no
  // onExit here, the runner sees the silent loop), or when the task ended on its own (a failed scan, the cover, a
  // Ladder's end) or was ended by prepareForSleep; the rows come back with their FULL refresh.
  if (shownOnPanel.load() && runner.viewEnds(micros())) {
    leaveView(endText());
    // This pass's input belonged to the test view: a Back or Confirm release here must not pop the screen or start a
    // run. The next pass serves the rows.
    runner.heartbeat(micros());
    return;
  }
  // A task a stop left running past its bound (logged) is joined once it ends.
  if (!shownOnPanel.load()) runner.reap();
  UiListActivity::loop();
  // The sweep task refuses its next scan once this is older than GrayRunner::COVERED_GAP_US.
  runner.heartbeat(micros());
}

void TransitionTestActivity::startRun() {
  if (runner.running()) return;
  app.clearTapFlash();
  // The task's fields, written before it is created (its creation orders them before its first read).
  runner.prepare();
  // The Mode row's mode. Slow's dwell is the preset's longest dose run in slots; its crossing waits for the run's own
  // measurement (TransitionScene::slotDone).
  scene.setMode(sweepMode, lab.preset().longestRunSlots());
  scene.setHold(holdOn);
  {
    RenderLock lock(*this);
    shownOnPanel = true;  // from here the render task draws nothing; no task is alive yet
  }
  if (runner.launch()) return;
  {
    RenderLock lock(*this);
    shownOnPanel = false;  // nothing was sent: the rows stay as they are, with the reason
    status = tr(STR_GAMES_DLAB_NO_TASK);
    buildRows();
  }
  requestUpdate();
}

void TransitionTestActivity::stepMode() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    sweepMode = GameDisplayLab::nextMode(sweepMode);
    rowItems[static_cast<size_t>(Row::Mode)].subtitle = modeText(sweepMode);
    rowItems[static_cast<size_t>(Row::Hold)].subtitle = holdText(holdOn, sweepMode);
  }
  LOG_INF("DLAB", "transition test mode: %s", modeName(sweepMode));
  requestUpdate();
}

void TransitionTestActivity::toggleHold() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    holdOn = !holdOn;
    rowItems[static_cast<size_t>(Row::Hold)].subtitle = holdText(holdOn, sweepMode);
  }
  LOG_INF("DLAB", "transition test hold: %s", holdOn ? "on" : "off");
  requestUpdate();
}

void TransitionTestActivity::cleanFromRows() {
  // The rows view has no sweep task (startRun creates it with the view, and the view ends only after the join), so
  // the panel is this task's.
  if (runner.running()) return;
  app.clearTapFlash();
  {
    // The lab's clean sends the framebuffer with a FULL refresh: white first. The rows' repaint follows it.
    RenderLock lock(*this);
    renderer.clearScreen(0xFF);
    lab.cleanNow();
  }
  LOG_INF("DLAB", "transition test: panel cleaned to white");
  requestUpdate();
}

const char* TransitionTestActivity::endText() const {
  switch (runner.end()) {
    case GrayRunner::End::Stopped:
    case GrayRunner::End::PaintFailed:  // never: the scene always paints
      return tr(STR_GAMES_DLAB_STOPPED);
    case GrayRunner::End::Covered:
      return tr(STR_GAMES_DLAB_COVERED);
    case GrayRunner::End::ScanFailed:
      return DisplayLabActivity::outcomeText(runner.failedScan());
    case GrayRunner::End::Finished:
      return tr(STR_GAMES_DLAB_FINISHED);
  }
  return tr(STR_GAMES_DLAB_STOPPED);
}

void TransitionTestActivity::leaveView(const char* outcome) {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    if (runner.statsReported()) {
      snprintf(statsText, sizeof(statsText), tr(STR_GAMES_DLAB_STATS),
               static_cast<unsigned>(runner.reportedTenths() / 10), static_cast<unsigned>(runner.reportedTenths() % 10),
               static_cast<unsigned>(runner.reportedMsPerScan()));
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

// ---- sweep task: the runner's client --------------------------------------------------------------------------------

bool TransitionTestActivity::onReplay(GrayModel& model, const GrayPreset& preset) {
  scene.restartFlips();
  scene.restartLadder();
  resetLadderStage();
  scene.applyAll(model, 0, preset);
  // Slow's crossing is sized once the paint has settled (`gray slow:` says it); Static's rectangles do not cross,
  // and the Ladder has none (its layout line follows).
  const bool ladder = scene.mode() == SweepMode::Ladder;
  char crossing[32];
  char rects[16];
  if (scene.slowPending()) {
    snprintf(crossing, sizeof(crossing), "pending measurement");
  } else if (scene.paintPending()) {
    snprintf(crossing, sizeof(crossing), "%lu ms after the paint", static_cast<unsigned long>(scene.sweepMs()));
  } else if (ladder) {
    snprintf(crossing, sizeof(crossing), "none");
  } else {
    snprintf(crossing, sizeof(crossing), "%lu ms", static_cast<unsigned long>(scene.sweepMs()));
  }
  if (ladder) {
    snprintf(rects, sizeof(rects), "no rectangles");
  } else {
    snprintf(rects, sizeof(rects), "%d px", scene.rectW());
  }
  // The Ladder holds at both its photos whatever the Hold row says (TransitionScene::holdsAtSpots).
  const char* hold = scene.holdOn() ? "on" : "off";
  if (ladder) hold = scene.holdOn() ? "on (the ladder always holds)" : "off, but the ladder always holds";
  LOG_INF("DLAB",
          "gray sweep: %u pixels to drive, mode %s, %s, crossing %s, hold %s, rate %u, swap %u, retarget dark "
          "%u light %u, history %u, weak row %u",
          static_cast<unsigned>(model.activeCount()), modeName(scene.mode()), rects, crossing, hold,
          static_cast<unsigned>(preset.rateIndex), static_cast<unsigned>(preset.swapPolarity),
          static_cast<unsigned>(preset.retargetDarkPercent), static_cast<unsigned>(preset.retargetLightPercent),
          static_cast<unsigned>(runner.hasHistory() ? preset.historyMode : 0), static_cast<unsigned>(runner.weakRow()));
  // With history on, a flip back to a pixel's origin is a completed return or a reversal, budgeted by the route's
  // return_delta or reverse_percent rather than the table dose.
  if (scene.mode() == SweepMode::Static && runner.hasHistory()) {
    LOG_INF("DLAB", "gray static: history %u is on, so the flips' residues include its route adjustments",
            static_cast<unsigned>(preset.historyMode));
  }
  if (scene.mode() == SweepMode::Ladder) logLadderLayout();
  return true;
}

void TransitionTestActivity::onPaused() {
  // A pause between two pulse slots of a Ladder stage puts a gap in its one-pulse-a-slot rhythm: its settle line
  // counts them and names the first few stage slots they fell after.
  if (scene.ladderMidStage()) {
    if (ladderPauses < LADDER_PAUSES_NAMED) {
      ladderPauseSlots[ladderPauses] = static_cast<uint8_t>(scene.ladderStageSlots());
    }
    ++ladderPauses;
  }
}

void TransitionTestActivity::onResume(GrayModel& model, const GrayPreset& preset) {
  // A run held at a photo spot: the marker goes back to its band's shade before anything else moves (the scene does
  // nothing unless it holds).
  if (!scene.resumeHold(model, preset)) return;
  LOG_INF("DLAB", "gray hold: resumed; marker back to %c", GameDisplayLab::shadeName(scene.markerShade()));
}

bool TransitionTestActivity::onCapClean() {
  // A hold in progress is dropped: repainted from white, the marker would settle before the pattern it marks.
  const std::optional<GameDisplayLab::DroppedHold> dropped = scene.cancelHold();
  if (dropped) {
    LOG_INF("DLAB", "gray hold: %s dropped by the session-cap clean after %lu marker slots: %s",
            GameDisplayLab::holdSpotName(dropped->spot), static_cast<unsigned long>(dropped->markerSlots),
            GameDisplayLab::holdDropName(dropped->drop));
  }
  // Static repaints from white: the sequence starts again at flip 0, so no settle line counts the repaint.
  if (scene.mode() == SweepMode::Static) {
    scene.restartFlips();
    LOG_INF("DLAB", "gray static: sequence restarted at flip 0 after the session-cap clean");
  }
  // The Ladder's columns are measured from a settled base: the run starts again from the base, which the reapplied
  // scene paints, unless the clean cut off the marker's way back from `ladder 2`: both photos are taken, and the run
  // ends as a finished one does.
  if (scene.mode() == SweepMode::Ladder) {
    resetLadderStage();
    if (scene.ladderCleaned(dropped)) {
      LOG_INF("DLAB", "gray ladder: done (the session-cap clean came after both photos)");
      return false;
    }
    LOG_INF("DLAB", "gray ladder: restarted from the base after the session-cap clean");
  }
  return true;
}

bool TransitionTestActivity::prepareSlot(GrayModel& model, const uint32_t prevMs, const uint32_t nowMs,
                                         const bool stale, const GrayPreset& preset) {
  if (stale) {
    scene.applyAll(model, nowMs, preset);
  } else {
    scene.update(model, prevMs, nowMs, preset);
  }
  return true;
}

GrayRunner::Client::SlotVerdict TransitionTestActivity::afterSlot(const GameDisplayLab::SlotResult& slot,
                                                                  const uint32_t slotStartUs, const uint32_t periodUs,
                                                                  const uint32_t elapsedMs) {
  // A Ladder stage's pulse slots, before slotDone moves the stage on: the longest slot, and the longest start-to-start
  // interval between two of them (a skipped slot period or a pause shows there, not in the slot's own length).
  if (scene.ladderPulsing() && slot.driven > 0) {
    if (periodUs > ladderSlotUs) ladderSlotUs = periodUs;
    if (ladderPulsed && slotStartUs - ladderLastStartUs > ladderIntervalUs) {
      ladderIntervalUs = slotStartUs - ladderLastStartUs;
    }
    ladderPulsed = true;
    ladderLastStartUs = slotStartUs;
  }
  const GameDisplayLab::SceneStep step =
      scene.slotDone(runner.model(), lab.preset(), slot.driven, slot.driveDone, periodUs, elapsedMs);
  if (step.settled) {
    LOG_INF("DLAB", "gray static: flip %lu settled after %lu slots (offset +%u, next +%u)",
            static_cast<unsigned long>(step.settled->flip), static_cast<unsigned long>(step.settled->slots),
            static_cast<unsigned>(step.settled->offset),
            static_cast<unsigned>(GameDisplayLab::TransitionScene::flipOffset(step.settled->flip + 1)));
  }
  if (step.ladderSettled) {
    logLadderSettled(*step.ladderSettled);
    resetLadderStage();
  }
  if (step.fastPainted) {
    LOG_INF("DLAB", "gray fast: paint settled after %lu slots; the sweep starts",
            static_cast<unsigned long>(*step.fastPainted));
  }
  if (step.measured) {
    // The paint's longest slot, which the crossing is sized from.
    const uint32_t slowUs = scene.slowPeriodUs();
    LOG_INF("DLAB", "gray slow: slot period %lu ms, crossing %lu ms, dwell %lu slots%s",
            static_cast<unsigned long>(slowUs / 1000), static_cast<unsigned long>(scene.sweepMs()),
            static_cast<unsigned long>(scene.dwellSlotsAt(slowUs)),
            scene.sweepCapped() ? " (capped at 120 s, under the longest dose run)" : "");
  }
  if (step.skipped) {
    LOG_INF("DLAB", "gray hold: %s %lu skipped (%s)", GameDisplayLab::holdSpotName(step.skipped->spot),
            static_cast<unsigned long>(step.skipped->count), GameDisplayLab::spotSkipName(step.skipped->reason));
  }
  if (step.hold) {
    // A photo spot: the scene put the marker out and stopped the sweep clock; the next slots drive the marker.
    LOG_INF("DLAB", "gray hold: %s %lu, marker %c over %c", GameDisplayLab::holdSpotName(step.hold->spot),
            static_cast<unsigned long>(step.hold->count), GameDisplayLab::shadeName(scene.markerShade()),
            GameDisplayLab::shadeName(scene.markerBandShade()));
  }
  if (step.held) {
    // Held for a photo: nothing is driven until Next (or Confirm) resumes, which drives the marker back first.
    runner.holdForPhoto();
    LOG_INF("DLAB", "gray hold: marker settled after %lu slots; holding, Next resumes",
            static_cast<unsigned long>(step.markerSlots));
  }
  if (step.resumed) {
    LOG_INF("DLAB", "gray hold: marker back after %lu slots; %s", static_cast<unsigned long>(step.markerSlots),
            step.finished ? "the ladder is done" : "the run goes on");
  }
  SlotVerdict verdict;
  // A Static flip retargets every pixel at the next slot.
  verdict.reapply = step.reapply;
  if (!step.finished) return verdict;
  // The Ladder's return settled, was held (the Ladder always holds) and resumed: the run ends as Finished, and the rows
  // come back with their FULL refresh.
  LOG_INF("DLAB", "gray ladder: done");
  verdict.finished = true;
  return verdict;
}

void TransitionTestActivity::resetLadderStage() {
  ladderPauses = 0;
  ladderSlotUs = 0;
  ladderIntervalUs = 0;
  ladderLastStartUs = 0;
  ladderPulsed = false;
}

void TransitionTestActivity::logLadderLayout() const {
  const GameDisplayLab::LadderLayout layout = scene.ladderLayout();
  const int rows = scene.ladderHeight();
  // Each half's rows, or "none" for a half without any (a screen under the marker's height).
  const auto range = [](char* out, const size_t size, const int first, const int count) {
    if (count <= 0) {
      snprintf(out, size, "none");
    } else {
      snprintf(out, size, "%d-%d", first, first + count - 1);
    }
  };
  char top[24];
  char bottom[24];
  range(top, sizeof(top), 0, rows / 2);
  range(bottom, sizeof(bottom), rows / 2, rows - rows / 2);
  LOG_INF("DLAB",
          "gray ladder: %d columns of %d px, gutter %d px; rows %s from white, %s from black, the marker's rows below "
          "black; painting the base",
          layout.columns, layout.width, layout.gutter, top, bottom);
  // A logical width under 48: 1 px columns, no gutter, as many as fit.
  if (layout.columns < GameDisplayLab::TransitionScene::LADDER_STEPS || layout.gutter == 0) {
    LOG_INF("DLAB", "gray ladder: narrow screen: %d of %d columns, %d px each, no gutter", layout.columns,
            GameDisplayLab::TransitionScene::LADDER_STEPS, layout.width);
  }
}

void TransitionTestActivity::logLadderSettled(const GameDisplayLab::LadderSettled& settled) const {
  const GrayPreset& preset = lab.preset();
  switch (settled.stage) {
    case GameDisplayLab::LadderStage::Base:
      LOG_INF("DLAB", "gray ladder: base painted after %lu slots; stage 1 (column n: n pulses from its origin)",
              static_cast<unsigned long>(settled.slots));
      return;
    case GameDisplayLab::LadderStage::Ladder:
    case GameDisplayLab::LadderStage::Return: {
      // What the photo needs beside it: the rate, the rhythm (and the pauses that broke it), the slot length, the
      // base's black and the columns.
      ScanBackend* backend = lab.backend();
      const char* rate = backend ? backend->rateLabel(preset.rateIndex) : nullptr;
      const GameDisplayLab::LadderLayout layout = scene.ladderLayout();
      const unsigned baseDose =
          preset.dose[static_cast<size_t>(Shade::W)][static_cast<size_t>(Shade::K)];  // 6>0, which painted the base
      // ", paused N times after stage slots a, b, c, d..." (columns n > 24 - slot carry each gap); nothing for none.
      char paused[64] = "";
      if (ladderPauses > 0) {
        int used = snprintf(paused, sizeof(paused), ", paused %lu times after stage slots",
                            static_cast<unsigned long>(ladderPauses));
        const uint32_t named = ladderPauses < LADDER_PAUSES_NAMED ? ladderPauses : LADDER_PAUSES_NAMED;
        for (uint32_t i = 0; i < named && used > 0 && static_cast<size_t>(used) < sizeof(paused); ++i) {
          used += snprintf(paused + used, sizeof(paused) - static_cast<size_t>(used), "%s %u", i == 0 ? "" : ",",
                           static_cast<unsigned>(ladderPauseSlots[i]));
        }
        if (ladderPauses > LADDER_PAUSES_NAMED && used > 0 && static_cast<size_t>(used) < sizeof(paused)) {
          snprintf(paused + used, sizeof(paused) - static_cast<size_t>(used), ", ...");
        }
      }
      LOG_INF("DLAB",
              "gray ladder: stage %u settled after %lu slots: rate %u (%s), swap %u, one pulse per slot%s, pulse "
              "interval up to %lu ms, slots up to %lu ms, base 6>0 dose %u, column n %s n pulses (end-aligned), %d px "
              "columns, gutter %d px",
              settled.stage == GameDisplayLab::LadderStage::Ladder ? 1u : 2u, static_cast<unsigned long>(settled.slots),
              static_cast<unsigned>(preset.rateIndex), rate ? rate : "?", static_cast<unsigned>(preset.swapPolarity),
              paused, static_cast<unsigned long>(ladderIntervalUs / 1000),
              static_cast<unsigned long>(ladderSlotUs / 1000), baseDose,
              settled.stage == GameDisplayLab::LadderStage::Ladder ? "has had" : "has been driven back", layout.width,
              layout.gutter);
      return;
    }
    case GameDisplayLab::LadderStage::Done:
      return;  // never settles: the run has ended
  }
}

#endif  // CROSSHATCH_DISPLAY_LAB

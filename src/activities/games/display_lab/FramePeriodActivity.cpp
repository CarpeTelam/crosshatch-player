#include "FramePeriodActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::LutRow;
using GameDisplayLab::PlaneFill;

namespace {

// The Run row's status after a run that ended with `result`.
const char* outcomeText(const DisplayLabActivity::Scan result) {
  switch (result) {
    case DisplayLabActivity::Scan::Done:
      return tr(STR_GAMES_DLAB_DONE);
    case DisplayLabActivity::Scan::TimedOut:
      return tr(STR_GAMES_DLAB_TIMED_OUT);
    case DisplayLabActivity::Scan::Dropped:
      return tr(STR_GAMES_DLAB_DROPPED);
    case DisplayLabActivity::Scan::Refused:
      return tr(STR_GAMES_DLAB_REFUSED);  // no planes, or an inverted display: the log line says which
  }
  return "";
}

}  // namespace

FramePeriodActivity::FramePeriodActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* FramePeriodActivity::headerTitle() const { return tr(STR_GAMES_DLAB_FRAME_PERIOD); }

void FramePeriodActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  for (size_t i = 0; i < SCANS; ++i) {
    const GameDisplayLab::ScanParams params = GameDisplayLab::framePeriodScan(i);
    snprintf(labels[i], sizeof(labels[i]), "tp=%u fr=0x%02X", static_cast<unsigned>(params.tp),
             static_cast<unsigned>(params.rate));
    snprintf(results[i], sizeof(results[i]), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  }
  buildRows();
}

void FramePeriodActivity::buildRows() {
  fui::ListItem runRow;
  runRow.label = tr(STR_GAMES_DLAB_RUN);
  runRow.subtitle = status;
  runRow.actionValue = 0;
  rowItems[0] = runRow;
  for (size_t i = 0; i < SCANS; ++i) {
    fui::ListItem item;
    item.label = labels[i];
    item.subtitle = results[i];
    item.actionValue = static_cast<int16_t>(1 + i);
    rowItems[1 + i] = item;
  }
}

void FramePeriodActivity::buildScreen(UiScreen& screen) {
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
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}

void FramePeriodActivity::activateIndex(const int index) {
  // Only the Run row does anything; a result row is a table line.
  if (index != 0 || running) return;
  run();
}

void FramePeriodActivity::run() {
  running = true;
  {
    // The render task reads the rows; the popup says the run is on while the scans block the loop task.
    RenderLock lock(*this);
    status = tr(STR_GAMES_DLAB_RUNNING);
    rowItems[0].subtitle = status;
    GUI.drawPopup(renderer, tr(STR_GAMES_DLAB_RUNNING));
  }
  // The one LUT buffer, rebuilt per scan: 112 bytes on the stack.
  uint8_t lut[GameDisplayLab::LUT_BYTES];
  const PlaneFill hold = GameDisplayLab::planeFillFor(LutRow::Hold);
  const char* outcome = tr(STR_GAMES_DLAB_DONE);
  for (size_t i = 0; i < SCANS; ++i) {
    const GameDisplayLab::ScanParams params = GameDisplayLab::framePeriodScan(i);
    const uint8_t tp = params.tp;
    const uint8_t rate = params.rate;
    // Rows 1 and 2 carry the polarity codes so the LUT is the same shape the polarity screen runs; with both planes
    // zero every pixel selects LUT0 and holds.
    GameDisplayLab::buildOneFrameLut(lut, tp, rate, GameDisplayLab::POLARITY_ROW1, GameDisplayLab::POLARITY_ROW2);
    uint32_t busyUs = 0;
    const DisplayLabActivity::Scan result = lab.scan(lut, hold.bw, hold.red, busyUs);
    RenderLock lock(*this);  // the render task reads the rows
    if (result != DisplayLabActivity::Scan::Done) {
      // The lab logged why (a timeout, after which it cleaned the panel, a dropped call, or a refusal); the run ends
      // here, and the
      // scans not reached keep "Not run".
      outcome = outcomeText(result);
      snprintf(results[i], sizeof(results[i]), "%s", outcome);
      break;
    }
    LOG_INF("DLAB", "tp=%u fr=0x%02x busy_us=%lu", static_cast<unsigned>(tp), static_cast<unsigned>(rate),
            static_cast<unsigned long>(busyUs));
    snprintf(results[i], sizeof(results[i]), tr(STR_GAMES_DLAB_MICROSECONDS), static_cast<unsigned>(busyUs));
  }
  {
    RenderLock lock(*this);
    status = outcome;
    rowItems[0].subtitle = status;
    running = false;
  }
  // The scans held every pixel, but the driver flagged a gray clear: the table is drawn with a FULL refresh.
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  requestUpdate();
}

void FramePeriodActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu
}

#endif  // CROSSHATCH_DISPLAY_LAB

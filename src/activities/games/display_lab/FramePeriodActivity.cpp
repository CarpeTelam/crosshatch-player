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

FramePeriodActivity::FramePeriodActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* FramePeriodActivity::headerTitle() const { return tr(STR_GAMES_DLAB_FRAME_PERIOD); }

void FramePeriodActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  // The lab opens this screen only with a backend; without one there is nothing to run.
  ScanBackend* backend = lab.backend();
  uint8_t rates = backend ? backend->rateCount() : 0;
  if (rates > MAX_RATES) {
    LOG_ERR("DLAB", "backend has %u rates; the frame-period screen runs the first %u", static_cast<unsigned>(rates),
            static_cast<unsigned>(MAX_RATES));
    rates = MAX_RATES;
  }
  rateCount = rates;
  scanCount = TP_COUNT * rateCount;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  for (size_t i = 0; i < scanCount; ++i) {
    snprintf(labels[i], sizeof(labels[i]), "tp=%u fr=%s", static_cast<unsigned>(tpOf(i)), rateLabelOf(i));
    snprintf(results[i], sizeof(results[i]), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  }
  buildRows();
}

const char* FramePeriodActivity::rateLabelOf(const size_t i) const {
  ScanBackend* backend = lab.backend();
  const char* label = backend ? backend->rateLabel(rateOf(i)) : nullptr;
  return label ? label : "?";
}

void FramePeriodActivity::buildRows() {
  fui::ListItem runRow;
  runRow.label = tr(STR_GAMES_DLAB_RUN);
  runRow.subtitle = status;
  runRow.actionValue = 0;
  rowItems[0] = runRow;
  for (size_t i = 0; i < scanCount; ++i) {
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
  props.count = static_cast<uint16_t>(listCount());
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
  // All-hold scans: both masks clear, once per run, so every pixel holds and the screen stays.
  GameDisplayLab::MaskPlanes& masks = lab.masks();
  masks.clear();
  ScanRequest request{
      .blackMask = masks.black(), .whiteMask = masks.white(), .frames = 0, .rate = 0, .swapPolarity = false};
  const char* outcome = tr(STR_GAMES_DLAB_DONE);
  for (size_t i = 0; i < scanCount; ++i) {
    const uint8_t tp = tpOf(i);
    request.frames = tp;
    request.rate = rateOf(i);
    uint32_t busyUs = 0;
    const DisplayLabActivity::Scan result = lab.scan(request, busyUs);
    RenderLock lock(*this);  // the render task reads the rows
    if (result != DisplayLabActivity::Scan::Done) {
      // The lab logged why (a timeout, after which it cleaned the panel, a dropped call, or a refusal); the run ends
      // here, and the scans not reached keep "Not run".
      outcome = DisplayLabActivity::outcomeText(result);
      snprintf(results[i], sizeof(results[i]), "%s", outcome);
      break;
    }
    LOG_INF("DLAB", "tp=%u fr=%s busy_us=%lu", static_cast<unsigned>(tp), rateLabelOf(i),
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

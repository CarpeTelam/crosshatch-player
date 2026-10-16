#include "PolarityActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::MaskPlanes;

PolarityActivity::PolarityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* PolarityActivity::headerTitle() const { return tr(STR_GAMES_DLAB_POLARITY); }

void PolarityActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  // The labels name what each row drives, from the backend that knows (the log line does the same).
  snprintf(labels[0], sizeof(labels[0]), tr(STR_GAMES_DLAB_ROW1), rowLabelOf(0));
  snprintf(labels[1], sizeof(labels[1]), tr(STR_GAMES_DLAB_ROW2), rowLabelOf(1));
  for (size_t i = 0; i < ROW_COUNT; ++i) snprintf(results[i], sizeof(results[i]), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  shownOnPanel = false;
  buildRows();
}

const char* PolarityActivity::rowLabelOf(const size_t index) const {
  const ScanBackend* backend = lab.backend();
  const char* label = backend ? backend->rowLabel(static_cast<uint8_t>(index + 1)) : nullptr;
  return label ? label : "?";
}

void PolarityActivity::buildRows() {
  for (size_t i = 0; i < ROW_COUNT; ++i) {
    fui::ListItem item;
    item.label = labels[i];
    item.subtitle = results[i];
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

void PolarityActivity::render(RenderLock&& lock) {
  // A routed tap and a selection move both request a repaint; while the panel shows a drive, none may run (the
  // repaint carries the cleaning FULL refresh). The next activation clears the flag and asks again.
  if (shownOnPanel.load()) return;
  UiListActivity::render(std::move(lock));
}

void PolarityActivity::buildScreen(UiScreen& screen) {
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
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void PolarityActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= ROW_COUNT) return;
  if (shownOnPanel.load()) {
    // The user has looked: this press only brings the screen back, with the cleaning refresh promoted by drive().
    shownOnPanel = false;
    requestUpdate();
    return;
  }
  // The row's tap flash would otherwise be drawn into the repaint that follows the drive.
  app.clearTapFlash();
  drive(static_cast<size_t>(index));
}

void PolarityActivity::drive(const size_t index) {
  MaskPlanes& masks = lab.masks();
  masks.clear();
  masks.fill(GameDisplayLab::POLARITY_ROW_MASKS[index]);
  // Rate index 0 (0x22 on the SSD1677) and the backend's rows unswapped: the drive its rowLabel names.
  const ScanRequest request{.blackMask = masks.black(),
                            .whiteMask = masks.white(),
                            .frames = GameDisplayLab::POLARITY_TP,
                            .rate = 0,
                            .swapPolarity = false};
  uint32_t busyUs = 0;
  const DisplayLabActivity::Scan result = lab.scan(request, busyUs);
  const unsigned ms = static_cast<unsigned>(busyUs / 1000);
  const unsigned row = static_cast<unsigned>(index + 1);
  RenderLock lock(*this);  // the render task reads the rows
  switch (result) {
    case DisplayLabActivity::Scan::Done:
      LOG_INF("DLAB", "polarity row=%u vs=%s ms=%u", row, rowLabelOf(index), ms);
      snprintf(results[index], sizeof(results[index]), tr(STR_GAMES_DLAB_DRIVEN), ms);
      // The panel keeps the drive until the next Confirm or tap; the repaint it brings cleans with a FULL refresh.
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      shownOnPanel = true;
      return;
    case DisplayLabActivity::Scan::TimedOut:
      // The lab logged the time and cleaned the panel, so there is nothing to look at.
      snprintf(results[index], sizeof(results[index]), "%s", tr(STR_GAMES_DLAB_TIMED_OUT));
      break;
    case DisplayLabActivity::Scan::Dropped:
      // The facade dropped the call (the lab logged it): the panel was not driven, so the screen comes back at once.
      snprintf(results[index], sizeof(results[index]), "%s", tr(STR_GAMES_DLAB_DROPPED));
      break;
    case DisplayLabActivity::Scan::Refused:
      LOG_ERR("DLAB", "polarity row=%u refused", row);
      snprintf(results[index], sizeof(results[index]), "%s", tr(STR_GAMES_DLAB_REFUSED));
      break;
  }
  requestUpdate();
}

void PolarityActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; leaving the lab cleans the panel
}

#endif  // CROSSHATCH_DISPLAY_LAB

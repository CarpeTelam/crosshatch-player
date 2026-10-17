#include "TuningActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayPreset;

namespace {

// InkBoy's shade letters, darkest first: transition names such as "K>D" are its notation, not translated text.
constexpr char SHADE_LETTERS[] = "KDLW";

}  // namespace

TuningActivity::TuningActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* TuningActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TUNING); }

TuningActivity::Kind TuningActivity::kindOf(const int row) {
  if (row < HOLD_FIRST) return Kind::Dose;
  if (row < RETARGET_DARK_ROW) return Kind::Hold;
  if (row == RETARGET_DARK_ROW) return Kind::RetargetDark;
  if (row == RETARGET_LIGHT_ROW) return Kind::RetargetLight;
  if (row == RATE_ROW) return Kind::Rate;
  if (row == SWAP_ROW) return Kind::Swap;
  if (row == RESET_ROW) return Kind::Reset;
  return Kind::Save;
}

uint8_t TuningActivity::transitionOf(const int row) {
  return static_cast<uint8_t>(row < HOLD_FIRST ? row - DOSE_FIRST : row - HOLD_FIRST);
}

uint8_t TuningActivity::rateCount() const {
  ScanBackend* backend = lab.backend();
  return backend ? backend->rateCount() : 0;
}

void TuningActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  editing = NOT_EDITING;
  for (uint8_t i = 0; i < GameDisplayLab::TRANSITION_COUNT; ++i) {
    const char name[4] = {SHADE_LETTERS[GameDisplayLab::transitionFrom(i)], '>',
                          SHADE_LETTERS[GameDisplayLab::transitionTo(i)], '\0'};
    snprintf(labels[DOSE_FIRST + i], sizeof(labels[0]), tr(STR_GAMES_DLAB_DOSE), name);
    snprintf(labels[HOLD_FIRST + i], sizeof(labels[0]), tr(STR_GAMES_DLAB_HOLD), name);
  }
  buildRows();
}

void TuningActivity::buildRows() {
  for (int row = 0; row < static_cast<int>(ROW_COUNT); ++row) {
    fui::ListItem item;
    switch (kindOf(row)) {
      case Kind::Dose:
      case Kind::Hold:
        item.label = labels[row];
        break;
      case Kind::RetargetDark:
        item.label = tr(STR_GAMES_DLAB_RETARGET_DARK);
        break;
      case Kind::RetargetLight:
        item.label = tr(STR_GAMES_DLAB_RETARGET_LIGHT);
        break;
      case Kind::Rate:
        item.label = tr(STR_GAMES_DLAB_FRAME_RATE);
        break;
      case Kind::Swap:
        item.label = tr(STR_GAMES_DLAB_SWAP_POLARITY);
        break;
      case Kind::Reset:
        item.label = tr(STR_GAMES_DLAB_RESET_PRESET);
        break;
      case Kind::Save:
        item.label = tr(STR_GAMES_DLAB_SAVE_PRESET);
        break;
    }
    item.subtitle = values[row];
    item.actionValue = static_cast<int16_t>(row);
    rowItems[row] = item;
    writeValue(row);
  }
}

bool TuningActivity::plainValue(const int row, char* out, const size_t capacity) const {
  const GrayPreset& preset = lab.preset();
  switch (kindOf(row)) {
    case Kind::Dose:
      snprintf(out, capacity, "%u", static_cast<unsigned>(preset.doseAt(transitionOf(row))));
      return true;
    case Kind::Hold:
      snprintf(out, capacity, "%s", preset.holdAt(transitionOf(row)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      return true;
    case Kind::RetargetDark:
      snprintf(out, capacity, tr(STR_GAMES_DLAB_PERCENT), static_cast<unsigned>(preset.retargetDarkPercent));
      return true;
    case Kind::RetargetLight:
      snprintf(out, capacity, tr(STR_GAMES_DLAB_PERCENT), static_cast<unsigned>(preset.retargetLightPercent));
      return true;
    case Kind::Rate: {
      ScanBackend* backend = lab.backend();
      const char* label = backend ? backend->rateLabel(preset.rateIndex) : nullptr;
      snprintf(out, capacity, "%s", label ? label : "?");
      return true;
    }
    case Kind::Swap:
      snprintf(out, capacity, "%s", preset.swapPolarity ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      return true;
    case Kind::Reset:
    case Kind::Save:
      return false;
  }
  return false;
}

void TuningActivity::writeValue(const int row) {
  char plain[24];
  if (!plainValue(row, plain, sizeof(plain))) {
    // The action rows say what they do; the Save row then keeps the line save() wrote.
    if (values[row][0] == '\0') {
      snprintf(values[row], sizeof(values[row]), "%s",
               kindOf(row) == Kind::Reset ? tr(STR_GAMES_DLAB_RESET_PRESET_DESC) : tr(STR_GAMES_DLAB_SAVE_PRESET_DESC));
    }
    return;
  }
  if (row == editing) {
    snprintf(values[row], sizeof(values[row]), tr(STR_GAMES_DLAB_EDITING), plain);
  } else {
    snprintf(values[row], sizeof(values[row]), "%s", plain);
  }
}

void TuningActivity::buildScreen(UiScreen& screen) {
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

template <typename Change>
void TuningActivity::edit(const int row, Change change) {
  {
    // The render task reads the preset's values through the rows' lines.
    RenderLock lock(*this);
    change(lab.preset());
    writeValue(row);
  }
  char plain[24];
  // buildRows gave every row a label.
  if (plainValue(row, plain, sizeof(plain))) LOG_INF("DLAB", "tune %s=%s", rowItems[row].label, plain);
  requestUpdate();
}

void TuningActivity::tapStep(const int row) {
  const uint8_t transition = transitionOf(row);
  const uint8_t rates = rateCount();
  switch (kindOf(row)) {
    case Kind::Dose:
      edit(row, [transition](GrayPreset& p) { p.cycleDose(transition); });
      return;
    case Kind::Hold:
      edit(row, [transition](GrayPreset& p) { p.toggleHold(transition); });
      return;
    case Kind::RetargetDark:
      edit(row, [](GrayPreset& p) { p.cycleRetarget(GameDisplayLab::Toward::Dark); });
      return;
    case Kind::RetargetLight:
      edit(row, [](GrayPreset& p) { p.cycleRetarget(GameDisplayLab::Toward::Light); });
      return;
    case Kind::Rate:
      edit(row, [rates](GrayPreset& p) { p.cycleRate(rates); });
      return;
    case Kind::Swap:
      edit(row, [](GrayPreset& p) { p.toggleSwap(); });
      return;
    case Kind::Reset:
      reset();
      return;
    case Kind::Save:
      save();
      return;
  }
}

void TuningActivity::adjust(const int row, const int direction) {
  const uint8_t transition = transitionOf(row);
  const uint8_t rates = rateCount();
  switch (kindOf(row)) {
    case Kind::Dose:
      edit(row, [transition, direction](GrayPreset& p) { p.stepDose(transition, direction); });
      return;
    case Kind::Hold:
      edit(row, [transition, direction](GrayPreset& p) { p.setHold(transition, direction > 0); });
      return;
    case Kind::RetargetDark:
      edit(row, [direction](GrayPreset& p) { p.stepRetarget(GameDisplayLab::Toward::Dark, direction); });
      return;
    case Kind::RetargetLight:
      edit(row, [direction](GrayPreset& p) { p.stepRetarget(GameDisplayLab::Toward::Light, direction); });
      return;
    case Kind::Rate:
      edit(row, [direction, rates](GrayPreset& p) { p.stepRate(direction, rates); });
      return;
    case Kind::Swap:
      edit(row, [direction](GrayPreset& p) { p.swapPolarity = direction > 0; });
      return;
    case Kind::Reset:
    case Kind::Save:
      return;  // never in edit mode
  }
}

void TuningActivity::setEditing(const int row) {
  {
    RenderLock lock(*this);
    const int previous = editing;
    editing = row;
    if (previous != NOT_EDITING) writeValue(previous);
    if (row != NOT_EDITING) writeValue(row);
  }
  requestUpdate();
}

void TuningActivity::save() {
  const bool saved = lab.savePreset();
  {
    RenderLock lock(*this);
    snprintf(values[SAVE_ROW], sizeof(values[SAVE_ROW]), "%s",
             saved ? tr(STR_GAMES_DLAB_SAVED) : tr(STR_GAMES_DLAB_SAVE_FAILED));
  }
  requestUpdate();
}

void TuningActivity::reset() {
  GameDisplayLab::PresetBase base = GameDisplayLab::PresetBase::InkBoy;
  {
    // The render task reads the preset's values through the rows' lines.
    RenderLock lock(*this);
    base = lab.resetPreset();
    for (int row = 0; row < static_cast<int>(ROW_COUNT); ++row) writeValue(row);
  }
  LOG_INF("DLAB", "tune reset: %s defaults", GameDisplayLab::presetBaseName(base));
  requestUpdate();
}

void TuningActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(ROW_COUNT)) return;
  // A tap ends edit mode, then acts on the row it landed on.
  if (editing != NOT_EDITING) setEditing(NOT_EDITING);
  tapStep(index);
}

bool TuningActivity::handleCustomInput() {
  if (editing == NOT_EDITING) return false;
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
    adjust(editing, +1);
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    adjust(editing, -1);
    return true;
  }
  return false;
}

bool TuningActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (editing != NOT_EDITING) {
      setEditing(NOT_EDITING);
    } else {
      onBackButton();
    }
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (editing != NOT_EDITING) {
      setEditing(NOT_EDITING);
    } else if (selected >= 0 && selected < static_cast<int>(ROW_COUNT)) {
      const Kind kind = kindOf(selected);
      if (kind == Kind::Save) {
        save();
      } else if (kind == Kind::Reset) {
        reset();
      } else {
        setEditing(selected);
      }
    }
    return true;
  }
  return false;
}

void TuningActivity::navigateButtons() {
  if (editing != NOT_EDITING) return;
  UiListActivity::navigateButtons();
}

void TuningActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; the edits stay in the session's preset
}

#endif  // CROSSHATCH_DISPLAY_LAB

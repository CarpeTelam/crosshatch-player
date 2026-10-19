#include "TuningActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <TransitionScene.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayPreset;
using GameDisplayLab::Shade;

TuningActivity::TuningActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* TuningActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TUNING); }

namespace {

using Toward = GameDisplayLab::Toward;

constexpr int SECTION_ROW = 0;

const char* sectionName(const GameDisplayLab::TuningSection section) {
  switch (section) {
    case GameDisplayLab::TuningSection::Doses:
      return tr(STR_GAMES_DLAB_SECTION_DOSES);
    case GameDisplayLab::TuningSection::Holds:
      return tr(STR_GAMES_DLAB_SECTION_HOLDS);
    case GameDisplayLab::TuningSection::Other:
      return tr(STR_GAMES_DLAB_SECTION_OTHER);
  }
  return "";
}

const char* historyName(const uint8_t mode) {
  switch (mode) {
    case 0:
      return tr(STR_STATE_OFF);
    case GameDisplayLab::HISTORY_RETURN:
      return tr(STR_GAMES_DLAB_HISTORY_RETURNS);
    case GameDisplayLab::HISTORY_REVERSE:
      return tr(STR_GAMES_DLAB_HISTORY_REVERSALS);
    default:
      return tr(STR_GAMES_DLAB_HISTORY_BOTH);
  }
}

// A dose or hold row whose transition has a via runs its legs' entries instead: `suffix` (a format taking the via's
// digit) after its value: " (via 2)" on a dose, read only for a pixel between those levels; " (via 2, unused)" on a
// hold, the legs' hold bits applying.
void appendVia(const GrayPreset& preset, const uint8_t transition, char* out, const size_t capacity,
               const char* suffix) {
  const uint8_t via = preset.viaAt(transition);
  if (via >= GameDisplayLab::SHADE_COUNT) return;
  const size_t used = strlen(out);
  if (used + 1 >= capacity) return;
  snprintf(out + used, capacity - used, suffix, GameDisplayLab::shadeName(static_cast<Shade>(via)));
}

}  // namespace

uint8_t TuningActivity::rateCount() const {
  ScanBackend* backend = lab.backend();
  return backend ? backend->rateCount() : 0;
}

void TuningActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  editing = NOT_EDITING;
  section = Section::Doses;
  saveLine = nullptr;
  buildRows();
}

void TuningActivity::buildRows() {
  rowCount = GameDisplayLab::tuningRowCount(section);
  for (int row = 0; row < rowCount; ++row) {
    fui::ListItem item;
    const Row kind = kindOf(row);
    switch (kind) {
      case Row::Section:
        item.label = tr(STR_GAMES_DLAB_SECTION);
        break;
      case Row::Dose:
      case Row::Hold:
      case Row::Via: {
        // The levels' digits (D14): transition names such as "0>1" (black to the darkest gray), not translated text.
        const uint8_t i = transitionOf(row);
        const char name[4] = {GameDisplayLab::shadeName(static_cast<Shade>(GameDisplayLab::transitionFrom(i))), '>',
                              GameDisplayLab::shadeName(static_cast<Shade>(GameDisplayLab::transitionTo(i))), '\0'};
        const char* format = kind == Row::Dose   ? tr(STR_GAMES_DLAB_DOSE)
                             : kind == Row::Hold ? tr(STR_GAMES_DLAB_HOLD)
                                                 : tr(STR_GAMES_DLAB_VIA);
        snprintf(labels[row], sizeof(labels[row]), format, name);
        item.label = labels[row];
        break;
      }
      case Row::RetargetDark:
        item.label = tr(STR_GAMES_DLAB_RETARGET_DARK);
        break;
      case Row::RetargetLight:
        item.label = tr(STR_GAMES_DLAB_RETARGET_LIGHT);
        break;
      case Row::History:
        item.label = tr(STR_GAMES_DLAB_HISTORY);
        break;
      case Row::ReturnDark:
        item.label = tr(STR_GAMES_DLAB_RETURN_DARK);
        break;
      case Row::ReturnLight:
        item.label = tr(STR_GAMES_DLAB_RETURN_LIGHT);
        break;
      case Row::ReverseDark:
        item.label = tr(STR_GAMES_DLAB_REVERSE_DARK);
        break;
      case Row::ReverseLight:
        item.label = tr(STR_GAMES_DLAB_REVERSE_LIGHT);
        break;
      case Row::Rate:
        item.label = tr(STR_GAMES_DLAB_FRAME_RATE);
        break;
      case Row::Swap:
        item.label = tr(STR_GAMES_DLAB_SWAP_POLARITY);
        break;
      case Row::Reset:
        item.label = tr(STR_GAMES_DLAB_RESET_PRESET);
        break;
      case Row::Save:
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
    case Row::Section:
      snprintf(out, capacity, "%s", sectionName(section));
      return true;
    case Row::Dose:
      snprintf(out, capacity, "%u", static_cast<unsigned>(preset.doseAt(transitionOf(row))));
      appendVia(preset, transitionOf(row), out, capacity, tr(STR_GAMES_DLAB_VIA_SUFFIX));
      return true;
    case Row::Hold:
      snprintf(out, capacity, "%s", preset.holdAt(transitionOf(row)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      appendVia(preset, transitionOf(row), out, capacity, tr(STR_GAMES_DLAB_VIA_UNUSED_SUFFIX));
      return true;
    case Row::Via: {
      const uint8_t via = preset.viaAt(transitionOf(row));
      if (via < GameDisplayLab::SHADE_COUNT) {
        snprintf(out, capacity, "%c", GameDisplayLab::shadeName(static_cast<Shade>(via)));
      } else {
        snprintf(out, capacity, "%s", tr(STR_NONE_OPT));
      }
      return true;
    }
    case Row::RetargetDark:
      snprintf(out, capacity, tr(STR_GAMES_DLAB_PERCENT), static_cast<unsigned>(preset.retargetDarkPercent));
      return true;
    case Row::RetargetLight:
      snprintf(out, capacity, tr(STR_GAMES_DLAB_PERCENT), static_cast<unsigned>(preset.retargetLightPercent));
      return true;
    case Row::History:
      snprintf(out, capacity, "%s", historyName(preset.historyMode));
      return true;
    case Row::ReturnDark:
    case Row::ReturnLight: {
      const Toward toward = kindOf(row) == Row::ReturnDark ? Toward::Dark : Toward::Light;
      snprintf(out, capacity, "%d", static_cast<int>(preset.returnDelta[GameDisplayLab::routeIndex(toward)]));
      return true;
    }
    case Row::ReverseDark:
    case Row::ReverseLight: {
      const Toward toward = kindOf(row) == Row::ReverseDark ? Toward::Dark : Toward::Light;
      const uint8_t percent = preset.reversePercent[GameDisplayLab::routeIndex(toward)];
      if (percent == 0) {
        snprintf(out, capacity, "%s", tr(STR_GAMES_DLAB_AS_RETARGET));
      } else {
        snprintf(out, capacity, tr(STR_GAMES_DLAB_PERCENT), static_cast<unsigned>(percent));
      }
      return true;
    }
    case Row::Rate: {
      ScanBackend* backend = lab.backend();
      const char* label = backend ? backend->rateLabel(preset.rateIndex) : nullptr;
      snprintf(out, capacity, "%s", label ? label : "?");
      return true;
    }
    case Row::Swap:
      snprintf(out, capacity, "%s", preset.swapPolarity ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      return true;
    case Row::Reset:
    case Row::Save:
      return false;
  }
  return false;
}

void TuningActivity::writeValue(const int row) {
  char plain[VALUE_CHARS];
  if (!plainValue(row, plain, sizeof(plain))) {
    // The action rows say what they do; the Save row, after a save, whether it worked.
    const char* line =
        lab.version1Notice() ? tr(STR_GAMES_DLAB_RESET_PRESET_VERSION1_DESC) : tr(STR_GAMES_DLAB_RESET_PRESET_DESC);
    if (kindOf(row) == Row::Save) line = saveLine ? saveLine : tr(STR_GAMES_DLAB_SAVE_PRESET_DESC);
    snprintf(values[row], sizeof(values[row]), "%s", line);
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
  props.count = static_cast<uint16_t>(rowCount);
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
  char plain[VALUE_CHARS];
  // buildRows gave every row a label.
  if (plainValue(row, plain, sizeof(plain))) LOG_INF("DLAB", "tune %s=%s", rowItems[row].label, plain);
  requestUpdate();
}

void TuningActivity::tapStep(const int row) {
  const uint8_t transition = transitionOf(row);
  const uint8_t rates = rateCount();
  switch (kindOf(row)) {
    case Row::Section:
      nextSection();
      return;
    case Row::Dose:
      edit(row, [transition](GrayPreset& p) { p.cycleDose(transition); });
      return;
    case Row::Hold:
      edit(row, [transition](GrayPreset& p) { p.toggleHold(transition); });
      return;
    case Row::Via:
      edit(row, [transition](GrayPreset& p) { p.cycleVia(transition); });
      return;
    case Row::RetargetDark:
      edit(row, [](GrayPreset& p) { p.cycleRetarget(Toward::Dark); });
      return;
    case Row::RetargetLight:
      edit(row, [](GrayPreset& p) { p.cycleRetarget(Toward::Light); });
      return;
    case Row::History:
      edit(row, [](GrayPreset& p) { p.cycleHistory(); });
      return;
    case Row::ReturnDark:
      edit(row, [](GrayPreset& p) { p.cycleReturnDelta(Toward::Dark); });
      return;
    case Row::ReturnLight:
      edit(row, [](GrayPreset& p) { p.cycleReturnDelta(Toward::Light); });
      return;
    case Row::ReverseDark:
      edit(row, [](GrayPreset& p) { p.cycleReversePercent(Toward::Dark); });
      return;
    case Row::ReverseLight:
      edit(row, [](GrayPreset& p) { p.cycleReversePercent(Toward::Light); });
      return;
    case Row::Rate:
      edit(row, [rates](GrayPreset& p) { p.cycleRate(rates); });
      return;
    case Row::Swap:
      edit(row, [](GrayPreset& p) { p.toggleSwap(); });
      return;
    case Row::Reset:
      reset();
      return;
    case Row::Save:
      save();
      return;
  }
}

void TuningActivity::adjust(const int row, const int direction) {
  const uint8_t transition = transitionOf(row);
  const uint8_t rates = rateCount();
  switch (kindOf(row)) {
    case Row::Dose:
      edit(row, [transition, direction](GrayPreset& p) { p.stepDose(transition, direction); });
      return;
    case Row::Hold:
      edit(row, [transition, direction](GrayPreset& p) { p.setHold(transition, direction > 0); });
      return;
    case Row::Via:
      edit(row, [transition, direction](GrayPreset& p) { p.stepVia(transition, direction); });
      return;
    case Row::RetargetDark:
      edit(row, [direction](GrayPreset& p) { p.stepRetarget(Toward::Dark, direction); });
      return;
    case Row::RetargetLight:
      edit(row, [direction](GrayPreset& p) { p.stepRetarget(Toward::Light, direction); });
      return;
    case Row::History:
      edit(row, [direction](GrayPreset& p) { p.stepHistory(direction); });
      return;
    case Row::ReturnDark:
      edit(row, [direction](GrayPreset& p) { p.stepReturnDelta(Toward::Dark, direction); });
      return;
    case Row::ReturnLight:
      edit(row, [direction](GrayPreset& p) { p.stepReturnDelta(Toward::Light, direction); });
      return;
    case Row::ReverseDark:
      edit(row, [direction](GrayPreset& p) { p.stepReversePercent(Toward::Dark, direction); });
      return;
    case Row::ReverseLight:
      edit(row, [direction](GrayPreset& p) { p.stepReversePercent(Toward::Light, direction); });
      return;
    case Row::Rate:
      edit(row, [direction, rates](GrayPreset& p) { p.stepRate(direction, rates); });
      return;
    case Row::Swap:
      edit(row, [direction](GrayPreset& p) { p.swapPolarity = direction > 0; });
      return;
    case Row::Section:
    case Row::Reset:
    case Row::Save:
      return;  // never in edit mode
  }
}

void TuningActivity::nextSection() {
  {
    // The render task reads the rows and their count.
    RenderLock lock(*this);
    section = GameDisplayLab::nextTuningSection(section);
    editing = NOT_EDITING;
    activeNav().selected = SECTION_ROW;
    buildRows();
  }
  requestUpdate();
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
    saveLine = saved ? tr(STR_GAMES_DLAB_SAVED) : tr(STR_GAMES_DLAB_SAVE_FAILED);
    if (section == Section::Other) writeValue(GameDisplayLab::TUNING_OTHER_SAVE_ROW);
  }
  requestUpdate();
}

void TuningActivity::reset() {
  GameDisplayLab::PresetBase base = GameDisplayLab::PresetBase::InkBoy;
  {
    // The render task reads the preset's values through the rows' lines.
    RenderLock lock(*this);
    base = lab.resetPreset();
    for (int row = 0; row < rowCount; ++row) writeValue(row);
  }
  LOG_INF("DLAB", "tune reset: %s defaults", GameDisplayLab::presetBaseName(base));
  requestUpdate();
}

void TuningActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount) return;
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
    } else if (selected >= 0 && selected < rowCount) {
      const Row kind = kindOf(selected);
      if (kind == Row::Save) {
        save();
      } else if (kind == Row::Reset) {
        reset();
      } else if (kind == Row::Section) {
        nextSection();
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

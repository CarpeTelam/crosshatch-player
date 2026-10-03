#if FREEINK_CAP_GAMES

#include "GameOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "GameModeActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

GameOptionsActivity::GameOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         const GameCore::ManifestSettings& settings, const uint8_t hostModes,
                                         GameSaveStore::Choices& choices, bool& changed)
    : UiListActivity(NAME, renderer, mappedInput),
      settings(settings),
      hostModes(hostModes),
      choices(choices),
      changed(changed) {}

const char* GameOptionsActivity::headerTitle() const { return tr(STR_GAMES_OPTIONS); }

void GameOptionsActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  buildRows();
}

void GameOptionsActivity::buildRows() {
  rowCount = 0;
  const auto add = [this](const int8_t setting, const char* label) {
    rowSetting[rowCount] = setting;
    fui::ListItem item;
    item.label = label;
    item.actionValue = static_cast<int16_t>(rowCount);
    rowItems[rowCount] = item;
    rowItems[rowCount].subtitle = valueOf(rowCount);
    ++rowCount;
  };
  // Mode only when there is a second mode to cycle to.
  if ((hostModes & (hostModes - 1)) != 0) add(MODE_ROW, tr(STR_GAMES_MODE));
  for (size_t i = 0; i < settings.count && rowCount < MAX_ROWS; ++i) {
    add(static_cast<int8_t>(i), settings.settings[i].name);
  }
}

const char* GameOptionsActivity::valueOf(const size_t row) const {
  if (rowSetting[row] == MODE_ROW) return GameModeActivity::modeName(choices.mode);
  const size_t index = static_cast<size_t>(rowSetting[row]);
  const GameCore::ManifestSetting& setting = settings.settings[index];
  const uint8_t chosen = choices.valueIndex[index] < setting.count ? choices.valueIndex[index] : setting.defaultIndex;
  return setting.values[chosen];
}

void GameOptionsActivity::buildScreen(UiScreen& screen) {
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

void GameOptionsActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount) return;
  const size_t row = static_cast<size_t>(index);
  {
    // The render task reads the choices and the row's value while it draws.
    RenderLock lock(*this);
    if (rowSetting[row] == MODE_ROW) {
      choices.mode = GameModeActivity::nextMode(choices.mode, hostModes);
      LOG_DBG("GAME", "Options: mode %u", static_cast<unsigned>(choices.mode));
    } else {
      const size_t setting = static_cast<size_t>(rowSetting[row]);
      const uint8_t count = settings.settings[setting].count;
      const uint8_t current =
          choices.valueIndex[setting] < count ? choices.valueIndex[setting] : settings.settings[setting].defaultIndex;
      choices.valueIndex[setting] = count > 0 ? static_cast<uint8_t>((current + 1) % count) : 0;
      LOG_DBG("GAME", "Options: %s = %s", settings.settings[setting].id,
              settings.settings[setting].values[choices.valueIndex[setting]]);
    }
    changed = true;
    rowItems[row].subtitle = valueOf(row);
  }
  requestUpdate();
}

void GameOptionsActivity::onBackButton() {
  app.clearTapFlash();
  // Pops to the title screen, whose result handler saves the choices and selects its Options row.
  finish();
}

#endif  // FREEINK_CAP_GAMES

#if FREEINK_CAP_GAMES

#include "GameModeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "GameMatchActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

using GameCore::Manifest;

struct ModeText {
  uint8_t bit;
  StrId name;
  StrId description;
  const char* log;  // the mode's name in the log
};

// The rows' order.
constexpr ModeText MODE_TEXTS[GameModeActivity::MAX_MODES] = {
    {Manifest::MODE_SOLO, StrId::STR_GAMES_MODE_SOLO, StrId::STR_GAMES_MODE_SOLO_DESC, "solo"},
    {Manifest::MODE_PASS, StrId::STR_GAMES_MODE_PASS, StrId::STR_GAMES_MODE_PASS_DESC, "pass"},
    {Manifest::MODE_NEARBY, StrId::STR_GAMES_MODE_NEARBY, StrId::STR_GAMES_MODE_NEARBY_DESC, "nearby"},
};

}  // namespace

int GameModeActivity::modeCount(const uint8_t modes) {
  int count = 0;
  for (const ModeText& mode : MODE_TEXTS)
    if ((modes & mode.bit) != 0) ++count;
  return count;
}

GameModeActivity::GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const GameCore::Manifest& manifest, const uint8_t modes)
    : UiListActivity(NAME, renderer, mappedInput), manifest(manifest) {
  // Static text, so built once here rather than on every buildScreen().
  for (size_t kind = 0; kind < GameModeActivity::MAX_MODES; ++kind) {
    const ModeText& mode = MODE_TEXTS[kind];
    if ((modes & mode.bit) == 0) continue;
    fui::ListItem item;
    item.label = I18N.get(mode.name);
    item.subtitle = I18N.get(mode.description);
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = static_cast<uint8_t>(kind);
    rowItems[rowCount] = item;
    ++rowCount;
  }
}

const char* GameModeActivity::headerTitle() const { return tr(STR_GAMES_MODE_TITLE); }

void GameModeActivity::buildScreen(UiScreen& screen) {
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
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void GameModeActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount) return;
  const ModeText& mode = MODE_TEXTS[rowKind[index]];
  // GameMatchActivity runs solo only (epic-pass-and-play passes the mode in), so every row starts that match.
  if (mode.bit == Manifest::MODE_SOLO) {
    LOG_INF("GAME", "Mode solo picked for %s", manifest.id);
  } else {
    LOG_INF("GAME", "Mode %s picked for %s: the match plays solo until it can run %s", mode.log, manifest.id, mode.log);
  }
  app.clearTapFlash();  // the row leaves this screen
  auto match = makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, manifest);
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
    return;
  }
  activityManager.replaceActivity(std::move(match));
}

void GameModeActivity::onBackButton() {
  app.clearTapFlash();
  // Pops to the launcher that pushed this screen (its selection and listing are as they were).
  finish();
}

#endif  // FREEINK_CAP_GAMES

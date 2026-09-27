#if FREEINK_CAP_GAMES

#include "GamesListActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "GameMatchActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* GAMES_DIR = "/.games";
constexpr size_t DIR_NAME_BUFFER = 64;
constexpr size_t PATH_BUFFER = 96;
constexpr size_t CHUNK_BYTES = 96;

bool nameLess(const GameCore::Manifest& a, const GameCore::Manifest& b) {
  return strcasecmp(a.name, b.name) < 0 || (strcasecmp(a.name, b.name) == 0 && std::strcmp(a.id, b.id) < 0);
}

}  // namespace

GamesListActivity::GamesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("GamesList", renderer, mappedInput) {}

const char* GamesListActivity::headerTitle() const { return tr(STR_GAMES_TITLE); }

void GamesListActivity::onEnter() {
  UiListActivity::onEnter();
  loadGames();
  rebuildRows();
}

bool GamesListActivity::readManifest(const char* dirName, GameCore::ManifestReader& reader, GameCore::Manifest& out) {
  char path[PATH_BUFFER];
  snprintf(path, sizeof(path), "%s/%s/manifest.json", GAMES_DIR, dirName);
  auto file = Storage.open(path);
  if (!file) {
    LOG_INF("GAME", "Skipping %s: no manifest.json", dirName);
    return false;
  }
  char chunk[CHUNK_BYTES];
  reader.begin();
  for (int n = file.read(chunk, sizeof(chunk)); n > 0; n = file.read(chunk, sizeof(chunk))) {
    reader.feed(chunk, static_cast<size_t>(n));
  }
  file.close();
  const GameCore::ManifestError error = reader.finish(out);
  if (error != GameCore::ManifestError::None) {
    LOG_INF("GAME", "Skipping %s: %s", dirName, GameCore::describe(error));
    return false;
  }
  if (std::strcmp(out.id, dirName) != 0) {
    LOG_INF("GAME", "Skipping %s: manifest id is %s", dirName, out.id);
    return false;
  }
  return true;
}

void GamesListActivity::loadGames() {
  games.reset();
  rows.reset();
  gameCount = 0;
  auto dir = Storage.open(GAMES_DIR);
  if (!dir || !dir.isDirectory()) return;

  size_t folders = 0;
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) ++folders;
  }
  const size_t capacity = std::min(folders, MAX_GAMES);
  if (capacity == 0) return;

  // Holds the JSON token buffer; reused for every manifest.
  auto reader = makeUniqueNoThrow<GameCore::ManifestReader>();
  // One entry per folder, at most MAX_GAMES (about 180 B each), sized once here.
  games = makeUniqueNoThrow<GameCore::Manifest[]>(capacity);
  if (!reader || !games) {
    LOG_ERR("GAME", "OOM: manifest reader (%u B) or %u games (%u B)",
            static_cast<unsigned>(sizeof(GameCore::ManifestReader)), static_cast<unsigned>(capacity),
            static_cast<unsigned>(capacity * sizeof(GameCore::Manifest)));
    games.reset();
    return;
  }

  char dirName[DIR_NAME_BUFFER];
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const size_t length = entry.getName(dirName, sizeof(dirName));
    const bool isGameFolder = entry.isDirectory() && length > 0 && length < sizeof(dirName) - 1 && dirName[0] != '.';
    entry.close();
    if (!isGameFolder) continue;
    if (gameCount >= capacity) {
      LOG_INF("GAME", "Listing the first %u games only", static_cast<unsigned>(capacity));
      break;
    }
    if (readManifest(dirName, *reader, games[gameCount])) ++gameCount;
  }
  std::sort(games.get(), games.get() + gameCount, nameLess);
  LOG_INF("GAME", "Found %u games", static_cast<unsigned>(gameCount));
}

void GamesListActivity::rebuildRows() {
  rows.reset();
  if (gameCount == 0) return;
  rows = makeUniqueNoThrow<fui::ListItem[]>(gameCount);
  if (!rows) {
    LOG_ERR("GAME", "OOM: %u list rows", static_cast<unsigned>(gameCount));
    gameCount = 0;  // nothing can be shown or opened without rows
    return;
  }
  for (size_t i = 0; i < gameCount; ++i) {
    rows[i].label = games[i].name;
    rows[i].actionValue = static_cast<int16_t>(i);
  }
}

void GamesListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (gameCount == 0) {
    screen.centeredText(tr(STR_GAMES_EMPTY), screen.theme().bodyText);
    return;
  }

  // rows was built in onEnter() and is reused on every repaint.
  fui::ListProps props;
  props.items = rows.get();
  props.count = static_cast<uint16_t>(gameCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void GamesListActivity::activateIndex(const int index) {
  app.clearTapFlash();  // the row leaves this screen
  auto match = makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, games[index]);
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    return;
  }
  activityManager.replaceActivity(std::move(match));
}

void GamesListActivity::onBackButton() {
  app.clearTapFlash();
  // Straight home (not finish()), so Home can reselect its Games row.
  activityManager.goHome();
}

#endif  // FREEINK_CAP_GAMES

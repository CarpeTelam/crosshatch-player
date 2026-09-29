#if FREEINK_CAP_GAMES

#include "GamesListActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "GameMatchActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "games/GamePackageInstaller.h"

namespace fui = freeink::ui;

namespace {

constexpr uint8_t NOTE_LINES = 4;

// What a person is told when an inbox file did not install.
const char* reasonText(const GamePackageInstaller::Error error) {
  using GamePackageInstaller::Error;
  switch (error) {
    case Error::SdCard:
      return tr(STR_GAMES_INSTALL_STORAGE);
    case Error::OutOfMemory:
      return tr(STR_GAMES_OUT_OF_MEMORY);
    case Error::NotAPackage:
      return tr(STR_GAMES_INSTALL_NOT_A_PACKAGE);
    case Error::BadManifest:
      return tr(STR_GAMES_INSTALL_BAD_MANIFEST);
    case Error::BadMember:
      return tr(STR_GAMES_INSTALL_BAD_MEMBER);
    case Error::NoMain:
      return tr(STR_GAMES_INSTALL_NO_MAIN);
    case Error::TooManyMembers:
      return tr(STR_GAMES_INSTALL_TOO_MANY);
    case Error::BadImage:
      return tr(STR_GAMES_BAD_IMAGE);
    case Error::PackageTooBig:
      return tr(STR_GAMES_INSTALL_PACKAGE_TOO_BIG);
    case Error::MemberTooBig:
      return tr(STR_GAMES_INSTALL_MEMBER_TOO_BIG);
    case Error::ImagesTooBig:
      return tr(STR_GAMES_INSTALL_IMAGES_TOO_BIG);
    case Error::BadSize:
      return tr(STR_GAMES_INSTALL_BAD_SIZE);
    case Error::BadCrc:
      return tr(STR_GAMES_INSTALL_BAD_CRC);
    case Error::BinaryLua:
      return tr(STR_GAMES_INSTALL_BINARY_LUA);
    case Error::Unsupported:
      return tr(STR_GAMES_INSTALL_UNSUPPORTED);
    case Error::BadDirectory:
      return tr(STR_GAMES_INSTALL_BAD_LIST);
    case Error::None:
      break;
  }
  return "";
}

}  // namespace

GamesListActivity::GamesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("GamesList", renderer, mappedInput) {}

const char* GamesListActivity::headerTitle() const { return tr(STR_GAMES_TITLE); }

void GamesListActivity::onEnter() {
  UiListActivity::onEnter();
  installInbox();
  loadGames();
  rebuildRows();
}

void GamesListActivity::installInbox() {
  noteVisible = false;
  if (GamePackageInstaller::hasInbox()) GUI.drawPopup(renderer, tr(STR_GAMES_INSTALLING));
  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  if (report.failed == 0) return;
  if (report.firstFile[0] == '\0') {
    snprintf(note, sizeof(note), "%s", reasonText(report.firstError));
  } else {
    snprintf(note, sizeof(note), "%s: %s", report.firstFile, reasonText(report.firstError));
  }
  noteVisible = true;
}

bool GamesListActivity::handleCustomInput() {
  if (!noteVisible) return false;
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    noteVisible = false;
    requestUpdate();
    return true;
  }
  return false;
}

void GamesListActivity::loadGames() {
  listing = GameRegistry::Listing{};
  rows.reset();
  if (!GameRegistry::load(listing)) return;
  // Solo only until the launcher's mode picker (epic-install-and-launcher): keep the games
  // that can start solo here, in the registry's order.
  size_t kept = 0;
  for (size_t i = 0; i < listing.count; ++i) {
    const GameRegistry::Entry& game = listing.entries[i];
    if (!game.check.ok() || (game.check.modes & GameCore::Manifest::MODE_SOLO) == 0) {
      LOG_INF("GAME", "Not listing %s: %s", game.manifest.id,
              game.check.ok() ? "no solo mode on this host" : GameCore::describe(game.check.reason));
      continue;
    }
    if (kept != i) listing.entries[kept] = game;
    ++kept;
  }
  listing.count = kept;
}

void GamesListActivity::rebuildRows() {
  rows.reset();
  if (listing.count == 0) return;
  rows = makeUniqueNoThrow<fui::ListItem[]>(listing.count);
  if (!rows) {
    LOG_ERR("GAME", "OOM: %u list rows", static_cast<unsigned>(listing.count));
    listing.count = 0;  // nothing can be shown or opened without rows
    return;
  }
  for (size_t i = 0; i < listing.count; ++i) {
    rows[i].label = listing.entries[i].manifest.name;
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

  if (listing.count == 0) {
    screen.centeredText(tr(STR_GAMES_EMPTY), screen.theme().bodyText);
  } else {
    // rows was built in onEnter() and is reused on every repaint.
    fui::ListProps props;
    props.items = rows.get();
    props.count = static_cast<uint16_t>(listing.count);
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    syncListViewport(screen, props);
    screen.list(props);
  }
  if (noteVisible) {
    // The reason wraps over a few lines, in a bordered panel so it reads against the list.
    fui::PopupProps popup;
    popup.message = note;
    popup.text = screen.theme().bodyText;
    popup.text.maxLines = NOTE_LINES;
    popup.text.align = fui::TextAlign::Center;
    popup.styles = screen.theme().popup;
    popup.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    popup.styles.normal.borderWidth = 2;
    screen.popup(popup);
  }
}

void GamesListActivity::activateIndex(const int index) {
  app.clearTapFlash();  // the row leaves this screen
  auto match = makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, listing.entries[index].manifest);
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

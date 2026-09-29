#if FREEINK_CAP_GAMES

#include "GamesLauncherActivity.h"

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
// Air above and below a row's icon, so the row is the icon plus this.
constexpr int16_t ROW_PADDING = 8;

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
    case Error::SourcesTooBig:
      return tr(STR_GAMES_SOURCES_TOO_LARGE);
    case Error::UnknownIcon:
      return tr(STR_GAMES_INSTALL_UNKNOWN_ICON);
    case Error::None:
      break;
  }
  return "";
}

// Whether a row opens: the host can start the game, and in solo, the only mode until the mode picker exists
// (epic-install-and-launcher entry 9). A game only another mode can start is not startable yet.
bool startable(const GameCore::CheckResult& check) {
  return check.ok() && (check.modes & GameCore::Manifest::MODE_SOLO) != 0;
}

// Why a game that is listed cannot start on this host, in a few words under its name.
const char* unavailableText(const GameCore::CheckResult& check) {
  using GameCore::CheckReason;
  if (check.ok()) return tr(STR_GAMES_UNAVAILABLE_MODE);  // Ok, but not in solo (startable)
  switch (check.reason) {
    case CheckReason::ApiTooNew:
      return tr(STR_GAMES_UNAVAILABLE_NEWER);
    case CheckReason::ApiTooOld:
      return tr(STR_GAMES_UNAVAILABLE_OLDER);
    case CheckReason::TooManySeats:
      return tr(STR_GAMES_UNAVAILABLE_SEATS);
    case CheckReason::NoHostMode:
      return tr(STR_GAMES_UNAVAILABLE_MODE);
    case CheckReason::None:
    case CheckReason::BadFields:
    case CheckReason::SoloNeedsOneSeat:
    case CheckReason::NearbyNeedsTwoSeats:
      break;
  }
  return tr(STR_GAMES_UNAVAILABLE_INVALID);
}

}  // namespace

GamesLauncherActivity::GamesLauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(NAME, renderer, mappedInput) {}

const char* GamesLauncherActivity::headerTitle() const { return tr(STR_GAMES_TITLE); }

void GamesLauncherActivity::onEnter() {
  UiListActivity::onEnter();
  installInbox();
  loadGames();
  loadIcons();
}

void GamesLauncherActivity::installInbox() {
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

bool GamesLauncherActivity::handleCustomInput() {
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

void GamesLauncherActivity::loadGames() {
  listing = GameRegistry::Listing{};
  // Every registry game is a row, in the registry's order; one this host cannot start says why on its row.
  if (!GameRegistry::load(listing)) return;
  for (size_t i = 0; i < listing.count; ++i) {
    const GameRegistry::Entry& game = listing.entries[i];
    if (!game.check.ok())
      LOG_INF("GAME", "Unavailable %s: %s", game.manifest.id, GameCore::describe(game.check.reason));
  }
}

void GamesLauncherActivity::loadIcons() {
  packageIcons.reset();
  packageSlot.reset();
  if (listing.count == 0) return;
  packageSlot = makeUniqueNoThrow<int16_t[]>(listing.count);
  if (!packageSlot) {
    LOG_ERR("GAME", "OOM: %u icon slots", static_cast<unsigned>(listing.count * sizeof(int16_t)));
  } else {
    // Sized by the games that have an icon.bmp, so the cache costs 512 B for each and nothing for the rest.
    size_t withFile = 0;
    for (size_t i = 0; i < listing.count; ++i) {
      const bool has = GameRowIcon::hasPackageIcon(listing.entries[i].manifest.id);
      packageSlot[i] = has ? 0 : NO_SLOT;  // 0 only marks "has a file"; the read below assigns the real slot
      if (has) ++withFile;
    }
    if (withFile > 0) packageIcons = makeUniqueNoThrow<uint8_t[]>(withFile * GameRowIcon::BYTES);
    if (withFile > 0 && !packageIcons) {
      LOG_ERR("GAME", "OOM: %u B of package icons", static_cast<unsigned>(withFile * GameRowIcon::BYTES));
    }
    int16_t next = 0;
    for (size_t i = 0; i < listing.count; ++i) {
      if (packageSlot[i] == NO_SLOT) continue;
      const bool read = packageIcons && GameRowIcon::readPackageIcon(
                                            listing.entries[i].manifest.id,
                                            packageIcons.get() + static_cast<size_t>(next) * GameRowIcon::BYTES);
      packageSlot[i] = read ? next++ : NO_SLOT;
    }
  }

  // Where each row's icon comes from: the choice provideRow draws.
  for (size_t i = 0; i < listing.count; ++i) {
    const GameRowIcon::Choice choice = choiceOf(i);
    switch (choice.source) {
      case GameRowIcon::Source::PackageBmp:
        LOG_DBG("GAME", "Icon for %s: icon.bmp", listing.entries[i].manifest.id);
        break;
      case GameRowIcon::Source::Library:
        LOG_DBG("GAME", "Icon for %s: library %s %s", listing.entries[i].manifest.id, choice.name,
                choice.fill ? "fill" : "regular");
        break;
      case GameRowIcon::Source::Fallback:
        LOG_DBG("GAME", "Icon for %s: fallback %s", listing.entries[i].manifest.id, choice.name);
        break;
    }
  }
}

GameRowIcon::Choice GamesLauncherActivity::choiceOf(const size_t index) const {
  const GameCore::Manifest& manifest = listing.entries[index].manifest;
  const bool packaged = packageSlot && packageSlot[index] != NO_SLOT;
  return GameRowIcon::choose(packaged, manifest.icon, manifest.iconWeight == GameCore::Manifest::ICON_FILL);
}

void GamesLauncherActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<GamesLauncherActivity*>(ctx);
  if (index >= self->listing.count) return;
  const GameRegistry::Entry& game = self->listing.entries[index];
  item.label = game.manifest.name;
  item.actionValue = static_cast<int16_t>(index);
  // The reason, not a dimmed row: a disabled state would hide the selection, which can rest on this row.
  if (!startable(game.check)) item.subtitle = unavailableText(game.check);

  // The package's icon is read already; a library icon is decoded into the one scratch, which the list draws from
  // (measure and draw share one provider call) before it asks for the next row.
  const uint8_t* bits = self->libraryIcon;
  const GameRowIcon::Choice choice = self->choiceOf(index);
  if (choice.source == GameRowIcon::Source::PackageBmp) {
    bits = self->packageIcons.get() + static_cast<size_t>(self->packageSlot[index]) * GameRowIcon::BYTES;
  } else {
    // A name choose() found in the library; the row is blank only if the library itself lost game-controller.
    GameRowIcon::renderLibraryIcon(choice.name, choice.fill, self->libraryIcon);
  }
  item.icon.data = bits;
  item.icon.width = GameRowIcon::SIDE;
  item.icon.height = GameRowIcon::SIDE;
  item.icon.format = fui::BitmapFormat::Mask1;  // bit 0 = ink, MSB first: how icon.bmp and the library store it
  item.icon.progmem = false;
}

void GamesLauncherActivity::buildScreen(UiScreen& screen) {
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
    // Rows are formatted on demand from the listing (provideRow), so no per-row array exists.
    fui::ListProps props;
    props.rowProvider = &GamesLauncherActivity::provideRow;
    props.rowProviderCtx = this;
    props.count = static_cast<uint16_t>(listing.count);
    props.iconSize = GameRowIcon::SIDE;
    // Icon and air, so the fixed-height page estimate is the rows' real height and paging lands on whole rows.
    props.rowHeight = static_cast<int16_t>(GameRowIcon::SIDE + 2 * ROW_PADDING);
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

void GamesLauncherActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= listing.count) return;
  const GameRegistry::Entry& game = listing.entries[index];
  if (!startable(game.check)) {
    LOG_INF("GAME", "Not starting %s: %s", game.manifest.id,
            game.check.ok() ? "no solo mode on this host" : GameCore::describe(game.check.reason));
    requestUpdate();  // the tap moved the selection here; show it
    return;
  }
  app.clearTapFlash();  // the row leaves this screen
  auto match = makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, game.manifest);
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    return;
  }
  activityManager.replaceActivity(std::move(match));
}

void GamesLauncherActivity::onBackButton() {
  app.clearTapFlash();
  // Straight home (not finish()), so Home can reselect its Games row.
  activityManager.goHome();
}

#endif  // FREEINK_CAP_GAMES

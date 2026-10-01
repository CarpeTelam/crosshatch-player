#if FREEINK_CAP_GAMES

#include "GamesLauncherActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "GameMatchActivity.h"
#include "GameModeActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "games/GameHostCaps.h"
#include "games/GamePackageInstaller.h"
#include "games/GameSaveStore.h"

namespace fui = freeink::ui;

namespace {

constexpr uint8_t NOTE_LINES = 4;
// Air above and below a row's icon, so the row is the icon plus this.
constexpr int16_t ROW_PADDING = 8;

// The game the launcher last opened, as an FNV-1a hash of its id (0: none), so the next launcher can select it. Four
// bytes of static RAM (AD-2 allows a mutable static up to 64 B, and this epic's share is 32 B); constinit, so it has
// no initializer to run.
constinit uint32_t lastOpened = 0;

uint32_t fingerprintOf(const char* id) {
  uint32_t hash = 2166136261u;
  for (; *id != '\0'; ++id) hash = (hash ^ static_cast<uint8_t>(*id)) * 16777619u;
  return hash != 0 ? hash : 1;  // 0 is "no game"
}

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
    case Error::TooManyGames:
      return tr(STR_GAMES_INSTALL_TOO_MANY_GAMES);
    case Error::None:
      break;
  }
  return "";
}

// Why a game that is listed cannot start on this host, in a few words under its name.
const char* unavailableText(const GameCore::CheckResult& check) {
  using GameCore::CheckReason;
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
    : UiListActivity(NAME, renderer, mappedInput, true) {}

void GamesLauncherActivity::forgetOpenedGame() { lastOpened = 0; }

const char* GamesLauncherActivity::headerTitle() const { return tr(STR_GAMES_TITLE); }

void GamesLauncherActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_REMOVE_CHOICE, &GamesLauncherActivity::onRemoveChoice, this);
  removeIndex = -1;
  installInbox();
  loadGames();
  loadContinue();
  loadIcons();
  selectRemembered();
}

size_t GamesLauncherActivity::paddedCount() const {
  const size_t rows = rowCount();
  const size_t page = pageRows.load();
  if (page <= 1 || rows <= page) return rows;
  return (rows + page - 1) / page * page;
}

void GamesLauncherActivity::selectRemembered() {
  if (lastOpened == 0) return;
  for (size_t i = 0; i < listing.count; ++i) {
    if (fingerprintOf(listing.entries[i].manifest.id) == lastOpened) {
      // The game's Continue row when it has one, else its own row below them; the first build shows the whole page
      // holding it. Not the own row of a game with a save: one Confirm there starts a New match, which replaces the
      // save the person has just left.
      size_t row = continueCount + i;
      for (size_t r = 0; r < continueCount; ++r) {
        if (continueOf[r] == i) {
          row = r;
          break;
        }
      }
      activeNav().requestSelection(static_cast<int>(row));
      return;
    }
  }
}

void GamesLauncherActivity::installInbox() {
  if (GamePackageInstaller::hasInbox()) GUI.drawPopup(renderer, tr(STR_GAMES_INSTALLING));
  showInstallNote(GamePackageInstaller::installAll());
}

void GamesLauncherActivity::showInstallNote(const GamePackageInstaller::Report& report) {
  noteVisible = false;
  noteWaiting[0] = '\0';
  noteMore[0] = '\0';
  if (report.failed == 0) return;
  if (report.firstFile[0] == '\0') {
    snprintf(note, sizeof(note), "%s", reasonText(report.firstError));
  } else {
    snprintf(note, sizeof(note), "%s: %s", report.firstFile, reasonText(report.firstError));
  }
  // The other failures are counted, not named, on lines under the first reason. They are the installer's own count
  // less the first: every file it judged and failed. Files it did not judge are not in it: those past the 32 a visit
  // takes (they wait for the next visit) and names over 62 bytes (skipped, logged). `failed` and `waiting` stop at
  // 255, so a saturated count reads as its floor ("and 254 more").
  const unsigned others = report.failed - 1u;
  // At the game limit every package judged after its manifest read waits for room (TooManyGames), valid or not, so
  // those are told apart from the ones that did not install for a reason of their own. `waiting` counts the first
  // failure too when it waits; it is not one of the others.
  unsigned waiting = report.waiting;
  if (report.firstError == GamePackageInstaller::Error::TooManyGames && waiting > 0) --waiting;
  waiting = std::min(waiting, others);
  if (waiting == 0) {
    if (others > 0) snprintf(noteMore, sizeof(noteMore), tr(STR_GAMES_INSTALL_AND_MORE), others);
  } else {
    snprintf(noteWaiting, sizeof(noteWaiting), tr(STR_GAMES_INSTALL_MORE_WAITING), waiting);
    if (others > waiting) {
      snprintf(noteMore, sizeof(noteMore), tr(STR_GAMES_INSTALL_AND_MORE_NOT_INSTALLED), others - waiting);
    }
  }
  noteVisible = true;
}

bool GamesLauncherActivity::handleCustomInput() {
  if (noteVisible) {
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
  return removeIndex >= 0 && handleRemoveInput();
}

bool GamesLauncherActivity::handleButtons() {
  // Not under the note: the hold would eat the release that dismisses it.
  if (!noteVisible && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, REMOVE_HOLD_MS)) {
    // Fires at the threshold, mid-hold; the release that follows is suppressed by the manager, so it cannot
    // land in the confirmation.
    openRemoveDialog(activeNav().selected);
    return true;
  }
  return UiListActivity::handleButtons();
}

// The base walks listCount(), which includes the blank rows that pad the last page; the selection must stay on games.
void GamesLauncherActivity::navigateButtons() {
  const int count = static_cast<int>(rowCount());
  auto& n = activeNav();
  buttonNavigator.onNextRelease([this, count, &n] { moveSelectionTo(ButtonNavigator::nextIndex(n.selected, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousIndex(n.selected, count)); });
  buttonNavigator.onNextContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::nextPageIndex(n.selected, count, n.inputPageRows())); });
  buttonNavigator.onPreviousContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousPageIndex(n.selected, count, n.inputPageRows())); });
}

void GamesLauncherActivity::onRowLongPress(const int index) { openRemoveDialog(index); }

// The hit rects are the last render's: after the confirmation opens (or under the note popup) they are still the
// list's rows until the next render, and a tap on one must not start a game or move the selection.
void GamesLauncherActivity::onRowAction(const fui::ActionEvent& event) {
  if (removeIndex >= 0 || noteVisible) return;
  UiListActivity::onRowAction(event);
}

void GamesLauncherActivity::openRemoveDialog(const int row) {
  // A blank padding row, an empty list, or the note over the list is not a game to ask about.
  if (row < 0 || static_cast<size_t>(row) >= rowCount() || noteVisible) return;
  app.clearTapFlash();
  if (static_cast<size_t>(row) < continueCount) {
    // A Continue row opens a save; the game's own row is where a game is removed. The press did its job: clear it.
    requestUpdate();
    return;
  }
  removeIndex = static_cast<int>(gameOfRow(static_cast<size_t>(row)));
  removeFocus = 0;  // Cancel: a stray Confirm keeps the game
  requestUpdate();
}

void GamesLauncherActivity::closeRemoveDialog() {
  app.clearTapFlash();
  removeIndex = -1;
  requestUpdate();
}

bool GamesLauncherActivity::handleRemoveInput() {
  using Button = MappedInputManager::Button;
  // Touch: render() registered the dialog's buttons; onRemoveChoice runs for a tap on one.
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return true;
  if (mappedInput.wasReleased(Button::Back)) {
    closeRemoveDialog();
  } else if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left) ||
             mappedInput.wasReleased(Button::NavPrevious)) {
    removeFocus = 0;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right) ||
             mappedInput.wasReleased(Button::NavNext)) {
    removeFocus = 1;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    if (removeFocus == 1) {
      confirmRemove();
    } else {
      closeRemoveDialog();
    }
  }
  return true;  // the confirmation owns every pass while it is open
}

void GamesLauncherActivity::onRemoveChoice(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<GamesLauncherActivity*>(user);
  if (self->removeIndex < 0) return;
  self->removeFocus = event.value == 1 ? 1 : 0;
  if (self->removeFocus == 1) {
    self->confirmRemove();
  } else {
    self->closeRemoveDialog();
  }
}

void GamesLauncherActivity::confirmRemove() {
  const int index = removeIndex;
  app.clearTapFlash();  // the dialog leaves this screen
  if (index < 0 || static_cast<size_t>(index) >= listing.count) {
    removeIndex = -1;
    requestUpdate();
    return;
  }
  // The id is copied: the reload below replaces the listing. The name is read before it (the failure note).
  char id[GameCore::Manifest::MAX_ID_BYTES + 1];
  snprintf(id, sizeof(id), "%s", listing.entries[index].manifest.id);

  GamePackageInstaller::Error error = GamePackageInstaller::Error::None;
  bool install = false;
  {
    // The render task reads the listing, the Continue rows, and the icon cache, which the reload below replaces.
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_GAMES_REMOVING));
    error = GamePackageInstaller::remove(id);
    removeIndex = -1;
    if (error == GamePackageInstaller::Error::None && lastOpened == fingerprintOf(id)) lastOpened = 0;
    // The failure note names the game while the listing still holds it; it is shown after the reload.
    if (error != GamePackageInstaller::Error::None) {
      snprintf(note, sizeof(note), "%s: %s", listing.entries[index].manifest.name, tr(STR_GAMES_REMOVE_FAILED));
    }
    // A removed game frees a place, so a package that waited for room installs now, as on entering, rather than on
    // the next visit ("remove one first" is what its note asked for).
    install = error == GamePackageInstaller::Error::None && GamePackageInstaller::hasInbox();
    if (install) GUI.drawPopup(renderer, tr(STR_GAMES_INSTALLING));
  }
  // Outside the lock: the install reads none of what the render task reads (the listing, the Continue rows, the icon
  // cache, the note), and takes seconds for a package with images. Nothing requests a render while it runs; a render
  // already queued draws the old listing, which is still whole. Only the second scope writes what the render reads.
  // Built in place (no default-constructed Report assigned from a returned one), so the frame holds one Report.
  const GamePackageInstaller::Report report =
      install ? GamePackageInstaller::installAll() : GamePackageInstaller::Report{};
  RenderLock lock(*this);
  if (install) showInstallNote(report);
  // The registry is the truth after a failure too: a game whose .pkg went is no longer listed.
  loadGames();
  loadContinue();
  loadIcons();
  const int count = static_cast<int>(listing.count);
  // The next game takes its place, on its own row below the Continue rows. The selection keeps the removed row's index,
  // so when the install after the remove adds a game that sorts before it, or finishes the remove of one, the row is a
  // neighbour's.
  activeNav().requestSelection(count > 0 ? static_cast<int>(continueCount) + std::min(index, count - 1) : 0);
  if (error != GamePackageInstaller::Error::None) {
    LOG_ERR("GAME", "Cannot remove %s", id);  // remove() logged the path that would not go; `note` is set above
    noteWaiting[0] = '\0';
    noteMore[0] = '\0';
    noteVisible = true;
  }
  requestUpdate();
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

void GamesLauncherActivity::loadContinue() {
  continueCount = 0;
  // GameRegistry::load lists at most MAX_GAMES games, the size of continueOf; the bound is repeated here because an
  // overrun would write past the array.
  for (size_t i = 0; i < listing.count && i < GameRegistry::MAX_GAMES; ++i) {
    const GameRegistry::Entry& game = listing.entries[i];
    if (!game.check.ok()) continue;  // a row that cannot open a match would not resume one
    // A save that could not be checked (Unreadable) still gets its row: hiding it would offer only the game's own row,
    // which starts a new match over what may be a good save. Continue reads the file again when it is tapped, and a
    // save it cannot read then is an error view, never a new match (GameMatchActivity::seedResume).
    if (GameSaveStore::peek(game.manifest.id, game.pkgHash) != GameSaveStore::SaveState::None) {
      continueOf[continueCount++] = static_cast<uint16_t>(i);
    }
  }
  LOG_DBG("GAME", "%u Continue rows of %u games", static_cast<unsigned>(continueCount),
          static_cast<unsigned>(listing.count));
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
  if (index >= self->rowCount()) {
    // A padding row (listCount()): blank, and disabled so it registers no touch.
    item.label = "";
    item.enabled = false;
    return;
  }
  const size_t gameIndex = self->gameOfRow(index);
  const GameRegistry::Entry& game = self->listing.entries[gameIndex];
  item.label = game.manifest.name;
  item.actionValue = static_cast<int16_t>(index);
  // A Continue row says so under the name. The other rows give the reason, not a dimmed row: a disabled state would
  // hide the selection, which can rest on this row.
  if (index < self->continueCount) {
    item.subtitle = tr(STR_GAMES_CONTINUE);
  } else if (!game.check.ok()) {
    item.subtitle = unavailableText(game.check);
  }

  // The package's icon is read already; a library icon is decoded into the one scratch, which the list draws from
  // (measure and draw share one provider call) before it asks for the next row.
  const uint8_t* bits = self->libraryIcon;
  const GameRowIcon::Choice choice = self->choiceOf(gameIndex);
  if (choice.source == GameRowIcon::Source::PackageBmp) {
    bits = self->packageIcons.get() + static_cast<size_t>(self->packageSlot[gameIndex]) * GameRowIcon::BYTES;
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

  if (removeIndex >= 0) {
    buildRemoveDialog(screen);  // the list is not built under it, so none of its rows takes a touch
    return;
  }
  if (listing.count == 0) {
    screen.centeredText(tr(STR_GAMES_EMPTY), screen.theme().bodyText);
  } else {
    // Rows are formatted on demand from the listing (provideRow), so no per-row array exists.
    fui::ListProps props;
    props.rowProvider = &GamesLauncherActivity::provideRow;
    props.rowProviderCtx = this;
    props.count = static_cast<uint16_t>(rowCount());
    props.iconSize = GameRowIcon::SIDE;
    // Icon and air, so the fixed-height page estimate is the rows' real height and paging lands on whole rows.
    props.rowHeight = static_cast<int16_t>(GameRowIcon::SIDE + 2 * ROW_PADDING);
    props.action = ACTION_ROW;
    props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);  // buttons stay in loop()
    // Whether this build follows a fresh selection request; a correction pass of the same follow must not be undone.
    const bool followsSelection = activeNav().followOnBuild.load();
    syncListViewport(screen, props);
    // Whole pages: the sync measured the rows a page holds, so pad the list to a multiple of it, and when the
    // selection was just followed show the whole page holding it (the follow pulls the minimum, which would leave
    // it at the bottom edge of a page that starts partway).
    auto& n = activeNav();
    const int page = std::max(1, n.visibleRows);
    pageRows.store(static_cast<uint16_t>(page));
    props.count = static_cast<uint16_t>(paddedCount());
    if (followsSelection && n.followPending && props.selectedIndex >= 0) {
      n.top = props.selectedIndex / page * page;
      props.topIndex = static_cast<uint16_t>(n.top);
    }
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
    // The other failures' lines, top to bottom: those that wait for room, then the rest.
    const char* moreLines[2] = {};
    int moreCount = 0;
    if (noteWaiting[0] != '\0') moreLines[moreCount++] = noteWaiting;
    if (noteMore[0] != '\0') moreLines[moreCount++] = noteMore;
    if (moreCount == 0) {
      screen.popup(popup);
    } else {
      // Each more-line is a line of its own under the reason. The renderer's word wrap does not break on '\n', so the
      // panel is laid out as Screen::popup lays it out, with one line more room at the bottom for each of them.
      fui::DrawTarget& target = screen.target();
      const fui::Rect bounds = screen.frame().safeRect();
      const fui::Insets pad = popup.padding;
      const int16_t lineHeight = target.lineHeight(popup.text.font);
      const int16_t moreHeight = static_cast<int16_t>(moreCount * lineHeight);
      const int16_t contentWidth =
          std::max<int16_t>(1, static_cast<int16_t>(bounds.width * 3 / 4 - pad.left - pad.right));
      fui::TextStyle moreStyle = popup.text;
      moreStyle.maxLines = 1;
      const fui::Size reason = fui::measureWrappedText(target, note, popup.text, contentWidth);
      int16_t widest = reason.width;
      for (int i = 0; i < moreCount; ++i) {
        widest =
            std::max(widest, std::min(target.measureText(moreStyle.font, moreLines[i], moreStyle).width, contentWidth));
      }
      const fui::Rect panel =
          fui::centeredRect(bounds, fui::Size{static_cast<int16_t>(widest + pad.left + pad.right),
                                              static_cast<int16_t>(reason.height + moreHeight + pad.top + pad.bottom)});
      popup.padding.bottom = static_cast<int16_t>(pad.bottom + moreHeight);  // the reason is drawn above that room
      fui::popup(screen.frame(), panel, popup);
      for (int i = 0; i < moreCount; ++i) {
        // Bottom-up from the panel's padding: the last line sits where e4-z3's one more-line sat.
        const int16_t y = static_cast<int16_t>(panel.bottom() - pad.bottom - (moreCount - i) * lineHeight);
        target.text(fui::Rect{static_cast<int16_t>(panel.x + pad.left), y,
                              static_cast<int16_t>(panel.width - pad.left - pad.right), lineHeight},
                    moreLines[i], moreStyle);
      }
    }
  }
}

void GamesLauncherActivity::activateIndex(const int row) {
  if (row < 0 || static_cast<size_t>(row) >= rowCount()) return;
  const bool resume = static_cast<size_t>(row) < continueCount;  // a Continue row
  const GameRegistry::Entry& game = listing.entries[gameOfRow(static_cast<size_t>(row))];
  if (!game.check.ok()) {
    LOG_INF("GAME", "Not starting %s: %s", game.manifest.id, GameCore::describe(game.check.reason));
    requestUpdate();  // the tap moved the selection here; show it
    return;
  }
  app.clearTapFlash();                           // the row leaves this screen
  lastOpened = fingerprintOf(game.manifest.id);  // the next launcher opens on this game's page
  // A Continue row resumes the solo match its save holds, so there is no mode to ask. A game the host can start in two
  // or more modes otherwise asks which one. The picker is pushed, so its Back returns to this list as it is; the match
  // replaces the picker, and with it the stack.
  if (!resume && GameModeActivity::needed(game.check.modes)) {
    auto picker = makeUniqueNoThrow<GameModeActivity>(renderer, mappedInput, game.manifest, game.check.modes);
    if (!picker) {
      LOG_ERR("GAME", "OOM: %u byte mode activity", static_cast<unsigned>(sizeof(GameModeActivity)));
      requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
      return;
    }
    activityManager.pushActivity(std::move(picker));
    return;
  }
  // A game with no solo mode starts its one mode: pass as an open pass match with the fewest seats it can have, and
  // nearby solo until epic-play-nearby passes it in. A Continue row resumes a solo match.
  GameCore::Roster roster = GameCore::Roster::solo();
  if (!resume && (game.check.modes & GameCore::Manifest::MODE_SOLO) == 0) {
    if ((game.check.modes & GameCore::Manifest::MODE_PASS) != 0) {
      const uint8_t seats =
          GameCore::passSeats(game.manifest.seatsMin, game.manifest.seatsMax, gameHostCaps().maxSeats);
      if (seats == 0) {
        LOG_ERR("GAME", "Cannot start %s in pass: seats %d..%d leave no pass match on this host", game.manifest.id,
                static_cast<int>(game.manifest.seatsMin), static_cast<int>(game.manifest.seatsMax));
        requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
        return;
      }
      roster = GameCore::Roster::pass(seats);
      LOG_INF("GAME", "%s offers only pass: a %u-seat pass match", game.manifest.id, static_cast<unsigned>(seats));
    } else {
      LOG_INF("GAME", "%s offers only nearby: the match plays solo until it can run nearby", game.manifest.id);
    }
  }
  auto match =
      makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, game.manifest, roster,
                                           resume ? GameMatchActivity::Start::Resume : GameMatchActivity::Start::New);
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
    return;
  }
  activityManager.replaceActivity(std::move(match));
}

void GamesLauncherActivity::buildRemoveDialog(UiScreen& screen) {
  // The input task can close the dialog (removeIndex = -1) while this runs on the render task: read it once.
  const int index = removeIndex;
  if (index < 0 || static_cast<size_t>(index) >= listing.count) return;
  fui::DialogOption options[2];
  options[0].label = tr(STR_CANCEL);
  options[1].label = tr(STR_GAMES_REMOVE);
  for (int i = 0; i < 2; ++i) {
    options[i].action = ACTION_REMOVE_CHOICE;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = removeFocus == i ? fui::StateFocused : fui::StateNormal;
  }
  fui::OptionDialogProps& props = dialogProps;
  props.title = tr(STR_GAMES_REMOVE_TITLE);
  props.headline = listing.entries[index].manifest.name;
  props.message = tr(STR_GAMES_REMOVE_KEPT);
  props.options = options;
  props.optionCount = 2;
  props.verticalOptions = true;
  props.titleText = screen.theme().smallText;
  props.titleText.bold = true;
  props.headlineText = screen.theme().bodyText;
  props.headlineText.maxLines = 2;  // a long name wraps; the dialog grows to fit
  props.messageText = screen.theme().smallText;
  props.messageText.maxLines = 2;
  props.buttonText = screen.theme().smallText;
  props.inputMask = fui::InputTouch;  // physical buttons stay in handleRemoveInput()
  // A framed panel, as OptionPopup draws it.
  const auto& metrics = UITheme::getInstance().getMetrics();
  props.styles = fui::defaultPopupStyles();
  props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = props.styles.normal;
  props.styles.focused = props.styles.normal;
  props.styles.active = props.styles.normal;
  props.styles.disabled = props.styles.normal;

  const fui::Rect body = screen.body();
  int16_t width = static_cast<int16_t>(renderer.getScreenWidth() * 3 / 4);
  if (width > body.width) width = body.width;
  const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
  fui::optionDialog(screen.frame(), fui::centeredRect(body, fui::Size{width, height}), props);
}

void GamesLauncherActivity::onBackButton() {
  app.clearTapFlash();
  // Straight home (not finish()), so Home can reselect its Games row.
  activityManager.goHome();
}

#endif  // FREEINK_CAP_GAMES

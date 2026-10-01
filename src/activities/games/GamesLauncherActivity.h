#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"
#include "games/GameRowIcon.h"

namespace GamePackageInstaller {
struct Report;
}

// The Games launcher (Home → Games; spine AD-22): when it opens it installs every /games/*.chgame
// (GamePackageInstaller), then lists every installed game as one row of icon and name, paged by the
// list. A row's icon is the package's icon.bmp, else the manifest's library icon in its weight, else
// `game-controller` (GameRowIcon). A game this host cannot start (Manifest::check) shows its reason
// under the name and does not open. A file that failed to install is explained once, in a popup.
// A row of a game the host can start pushes the game's title screen (GameModeActivity), which offers its save
// (Continue) and its modes, so Back returns to this list as it was. The launcher reads no save: the title screen peeks
// it when it opens. A registry load that runs out of memory leaves no rows and says "Not enough memory", not "No games
// found". A long-press on a row (or a hold of Confirm) asks whether to remove that game; Remove deletes its folder
// (GamePackageInstaller::remove) and keeps its saved data, and a failure is explained in the note popup. After a
// remove that succeeds, the inbox is installed at once when it holds a file (a package that waited for room takes the
// freed place), as on entering, and the listing is read after it. That install also tries to finish the removes that
// stopped partway (installAll's finishRemovals, at most 32 a call), so a game whose remove failed earlier but kept its
// .removing marker goes at that moment, not the next time Games opens (one that still will not go stays, logged).
// The list pages by whole pages: it is padded with blank rows to a whole number of pages, so the last page does not
// repeat rows of the one before it. The launcher remembers the game it last opened (a fingerprint of its id), and the
// next launcher, built by ActivityManager::goToGames(), selects that game's row and shows the whole page holding it: a
// Confirm there opens the title screen, whose first row is Continue when the game has a save, so two Confirms resume.
class GamesLauncherActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GamesLauncher";
  // How long Confirm is held to ask about removing the selected game (the long-press of a button-only device).
  // The library's delete hold is 1000 ms; well under 500 would let an ordinary press of Confirm ask about removing,
  // and GameRemoveLauncherTest pins the value by holding Confirm one millisecond short of it and then at it.
  static constexpr unsigned long REMOVE_HOLD_MS = 1000;
  static_assert(REMOVE_HOLD_MS >= 500 && REMOVE_HOLD_MS <= 3000, "a Confirm hold that asks about removing a game");

  GamesLauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Forgets the game the launcher last opened, so the next launcher opens on the top. A remove of that game and a
  // restart do the same; this is for a test that must start from a fresh boot.
  static void forgetOpenedGame();

 private:
  // The listing's count rounded up to a whole number of pages: the rows past the listing are blank and inert.
  int listCount() const override { return static_cast<int>(paddedCount()); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int row) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;
  void onRowLongPress(int index) override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;

  // Installs the inbox, showing "Installing" while it works, and sets the note from its report (showInstallNote).
  void installInbox();
  // Sets the note from an install's report: the first failure's reason, and under it the other failures by kind (those
  // that wait for room, then the rest), or no note when nothing failed.
  void showInstallNote(const GamePackageInstaller::Report& report);
  // Reads the listing from the registry; a load that runs out of memory leaves it empty and sets listFailed.
  void loadGames();
  // Reads each game's icon.bmp once, and says where every row's icon comes from.
  void loadIcons();
  // Where row `index`'s icon comes from: what provideRow draws and loadIcons logs.
  GameRowIcon::Choice choiceOf(size_t index) const;
  // fui::ListProps::rowProvider: row `index`'s name, reason, and icon, on the render task.
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  static constexpr int16_t NO_SLOT = -1;
  static constexpr freeink::ui::ActionId ACTION_REMOVE_CHOICE = ACTION_USER;

  // Rows with a game behind them: one for each game in the listing, row i being listing.entries[i].
  size_t rowCount() const { return listing.count; }
  size_t paddedCount() const;
  // Selects the game the launcher last opened, when the listing still has it.
  void selectRemembered();
  // The remove confirmation: opened on a row, answered by Cancel (Back), or Remove.
  void openRemoveDialog(int row);
  void closeRemoveDialog();
  void confirmRemove();
  bool handleRemoveInput();
  void buildRemoveDialog(UiScreen& screen);
  static void onRemoveChoice(const freeink::ui::ActionEvent& event, void* user);

  // Fixed-size arrays sized once per visit: growing containers would abort on OOM.
  GameRegistry::Listing listing;  // every installed game, by name
  // Whether the last loadGames() ran out of memory (its listing is then empty): the list says so instead of "No games
  // found". Written by loadGames, read by buildScreen, as noteVisible.
  bool listFailed = false;
  // The bitmaps of the games that have an icon.bmp, GameRowIcon::BYTES each in slot order, and the slot of
  // each game (parallel to listing.entries; NO_SLOT for none). Both are read-only after onEnter().
  std::unique_ptr<uint8_t[]> packageIcons;
  std::unique_ptr<int16_t[]> packageSlot;
  // The library icon of the row being drawn. Written by provideRow, on the render task only; the list
  // reads it before it asks for the next row.
  uint8_t libraryIcon[GameRowIcon::BYTES] = {};
  // The one-time install failure notice: shown over the list until a tap or button dismisses it.
  char note[128] = {};
  // The other failures of an install, each drawn as a line of its own under the note (noteWaiting above noteMore), and
  // each empty when it has nothing to say. noteWaiting: "N more waiting for room", the others that wait for room.
  // noteMore: "and N more not installed" for the rest when some wait, else "and N more" for all of them.
  char noteWaiting[48] = {};
  char noteMore[48] = {};
  bool noteVisible = false;
  // Rows a page holds, as the last build measured them (1 until the first build); listCount() pads to it.
  std::atomic<uint16_t> pageRows{1};
  // The listing index (not a row) the open remove confirmation asks about (-1: none), and its focused button (0 Cancel,
  // 1 Remove).
  int removeIndex = -1;
  uint8_t removeFocus = 0;
  // The confirmation's props, filled by each buildRemoveDialog (about 700 B: too big for the render task's stack).
  freeink::ui::OptionDialogProps dialogProps;
};

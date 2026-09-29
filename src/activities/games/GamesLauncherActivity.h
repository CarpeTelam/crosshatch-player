#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"
#include "games/GameRowIcon.h"

// The Games launcher (Home → Games; spine AD-22): when it opens it installs every /games/*.cpgame
// (GamePackageInstaller), then lists every installed game as a row of icon and name, paged by the
// list. A row's icon is the package's icon.bmp, else the manifest's library icon in its weight, else
// `game-controller` (GameRowIcon). A game this host cannot start (Manifest::check) shows its reason
// under the name and does not open. A file that failed to install is explained once, in a popup.
// Opening a game replaces this screen with its match, or, for a game the host can start in two or more modes, opens
// the mode picker (GameModeActivity) above it.
// Above the game rows the list has one "Continue" row (the game's name and icon, "Continue" under it) for each game
// whose GameSaveStore::peek() finds a valid resume.bin for the installed package, or one it could not check (a card or
// heap fault: the row stays rather than the launcher offering a new match over a save it cannot see), in the games'
// name order. A tap replaces this screen with the match resumed from the save, with no mode step. A save of a changed
// package is not valid, so it has no row. The rows are found when the listing is built (onEnter, and after a remove),
// never while a row is drawn. A long-press on a Continue row does nothing: removing is on the game's own row. A
// long-press on a row (or a hold of Confirm) asks whether to remove that game; Remove deletes its folder
// (GamePackageInstaller::remove) and keeps its saved data, and a failure is explained in the note popup.
// The list pages by whole pages: it is padded with blank rows to a whole number of pages, so the last page does not
// repeat rows of the one before it (Continue rows are rows of the list like the games'). The launcher remembers the
// game it last opened (a fingerprint of its id), and the next launcher, built by ActivityManager::goToGames(), selects
// that game's Continue row when it has one (a Confirm on its own row would start a New match over the save), else its
// own row, and shows the page holding it.
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

  // Installs the inbox, showing "Installing" while it works, and keeps the first failure for the popup.
  void installInbox();
  void loadGames();
  // Finds the Continue rows for the loaded listing: one GameSaveStore::peek() for each game that can start, so a card
  // with no save costs two existence checks a game and each save one whole-file read.
  void loadContinue();
  // Reads each game's icon.bmp once, and says where every row's icon comes from.
  void loadIcons();
  // Where row `index`'s icon comes from: what provideRow draws and loadIcons logs.
  GameRowIcon::Choice choiceOf(size_t index) const;
  // fui::ListProps::rowProvider: row `index`'s name, reason, and icon, on the render task.
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  static constexpr int16_t NO_SLOT = -1;
  static constexpr freeink::ui::ActionId ACTION_REMOVE_CHOICE = ACTION_USER;

  // Rows with a game behind them: the Continue rows, then one for each game in the listing.
  size_t rowCount() const { return continueCount + listing.count; }
  // The listing index of the game row `row` (below rowCount()) opens: a Continue row's own (continueOf is set whenever
  // continueCount is not 0), else row - continueCount.
  size_t gameOfRow(size_t row) const { return row < continueCount ? continueOf[row] : row - continueCount; }
  size_t paddedCount() const;
  // Selects the game the launcher last opened, when the listing still has it: its Continue row, else its own row.
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
  // The listing index of the game of each Continue row, in listing order; the first continueCount are filled.
  std::unique_ptr<uint16_t[]> continueOf;
  size_t continueCount = 0;
  // The bitmaps of the games that have an icon.bmp, GameRowIcon::BYTES each in slot order, and the slot of
  // each game (parallel to listing.entries; NO_SLOT for none). Both are read-only after onEnter().
  std::unique_ptr<uint8_t[]> packageIcons;
  std::unique_ptr<int16_t[]> packageSlot;
  // The library icon of the row being drawn. Written by provideRow, on the render task only; the list
  // reads it before it asks for the next row.
  uint8_t libraryIcon[GameRowIcon::BYTES] = {};
  // The one-time install failure notice: shown over the list until a tap or button dismisses it.
  char note[128] = {};
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

#pragma once

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
// Opening a game replaces this screen with its match.
class GamesLauncherActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GamesLauncher";

  GamesLauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  int listCount() const override { return static_cast<int>(listing.count); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;

  // Installs the inbox, showing "Installing" while it works, and keeps the first failure for the popup.
  void installInbox();
  void loadGames();
  // Reads each game's icon.bmp once, and says where every row's icon comes from.
  void loadIcons();
  // Where row `index`'s icon comes from: what provideRow draws and loadIcons logs.
  GameRowIcon::Choice choiceOf(size_t index) const;
  // fui::ListProps::rowProvider: row `index`'s name, reason, and icon, on the render task.
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  static constexpr int16_t NO_SLOT = -1;

  // Fixed-size arrays sized once per visit: growing containers would abort on OOM.
  GameRegistry::Listing listing;  // every installed game, by name
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
};

#pragma once

#include <cstddef>
#include <memory>

#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"

// Minimal Games list (Home → Games): when it opens it installs every /games/*.cpgame
// (GamePackageInstaller), then lists the registry's games that can start solo on this
// host, by name. A file that failed to install is explained once, in a popup. Opening a
// game replaces this screen with its match.
class GamesListActivity final : public UiListActivity {
 public:
  GamesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

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
  void rebuildRows();

  // Fixed-size arrays sized once per visit: growing containers would abort on OOM.
  GameRegistry::Listing listing;                  // the games that can start here, by name
  std::unique_ptr<freeink::ui::ListItem[]> rows;  // row i shows listing.entries[i]
  // The one-time install failure notice: shown over the list until a tap or button dismisses it.
  char note[128] = {};
  bool noteVisible = false;
};

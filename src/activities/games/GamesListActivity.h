#pragma once

#include <Manifest.h>

#include <vector>

#include "activities/UiListActivity.h"

namespace GameCore {
class ManifestReader;
}

// Minimal Games list (Home → Games): each /.games/<id>/ whose manifest.json parses
// and names the same id, by name. Opening one replaces this screen with its match.
class GamesListActivity final : public UiListActivity {
 public:
  GamesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr size_t MAX_GAMES = 64;

  int listCount() const override { return static_cast<int>(games.size()); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  void loadGames();
  // Parses /.games/<dirName>/manifest.json into out; false (logged) when it is
  // missing, invalid, or names another id.
  static bool readManifest(const char* dirName, GameCore::ManifestReader& reader, GameCore::Manifest& out);
  void rebuildRows();

  std::vector<GameCore::Manifest> games;
  std::vector<freeink::ui::ListItem> rows;
};

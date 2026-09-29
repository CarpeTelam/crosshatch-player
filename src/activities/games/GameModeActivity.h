#pragma once

#include <Manifest.h>

#include <cstddef>
#include <cstdint>

#include "activities/UiListActivity.h"

// The mode picker (Home → Games → a game → a mode; spine AD-22): the modes Manifest::check leaves for this host, one
// row each, in the order solo, pass and play, play nearby. The launcher pushes it only when there are two or more
// (needed), so a person with one mode never sees it. A tap or Confirm on a row starts the game's match; Back pops to
// the launcher.
//
// The match runs solo whatever row is picked: it is the only mode GameMatchActivity can run today, and a pick of
// pass or nearby is logged and starts that same match. epic-pass-and-play passes the picked mode into the match
// (and adds the seat choice here).
class GameModeActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GameMode";

  // The Manifest::Mode bits there are, and so the most rows.
  static constexpr size_t MAX_MODES = 3;

  // How many of the Manifest::Mode bits `modes` has set (bits past the three are not modes).
  static int modeCount(uint8_t modes);
  // Whether a game offering `modes` needs the picker: it has two or more.
  static bool needed(const uint8_t modes) { return modeCount(modes) >= 2; }

  // `modes` are Manifest::Mode bits, CheckResult::modes of the game's check; `manifest` is copied.
  GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest,
                   uint8_t modes);

 private:
  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  GameCore::Manifest manifest;
  // The rows, built once in the constructor, and the mode each one starts (its index in the cpp's mode table).
  uint8_t rowKind[MAX_MODES] = {};
  freeink::ui::ListItem rowItems[MAX_MODES] = {};
  size_t rowCount = 0;
};

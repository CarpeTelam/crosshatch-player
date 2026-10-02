#pragma once

#include <Manifest.h>

#include <cstddef>
#include <cstdint>

#include "activities/UiListActivity.h"
#include "games/GameSaveStore.h"

// A game's Options (title screen → Options; EXPERIENCE.md, Options row; DESIGN.md, Options row), headed "Options": a
// Mode row first, only when the host can start more than one of the game's modes, then one row per setting the
// manifest declares, in its order. Each row is the name ("Mode", or the setting's name) over its current value. A tap
// or Confirm sets the next value in place, wrapping after the last (modes in solo, pass, nearby order); nothing else
// is chosen here, and there is no list of values. Back (or the header's arrow, or the Back swipe) closes it.
//
// It edits the title screen's choices through the reference it is given, which outlives it (the title screen is under
// it on the stack), and sets `changed` on every edit; the title screen's result handler writes prefs.bin and redraws
// once this screen is gone. This screen reads and writes no card.
class GameOptionsActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "GameOptions";
  // The rows there can be: Mode, then one per setting.
  static constexpr size_t MAX_ROWS = 1 + GameCore::ManifestSettings::MAX_SETTINGS;

  // `settings` and `choices` are the title screen's, and outlive this screen; `hostModes` is the game's
  // CheckResult::modes on this host.
  GameOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                      const GameCore::ManifestSettings& settings, uint8_t hostModes, GameSaveStore::Choices& choices,
                      bool& changed);

 private:
  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  // The rows for the modes and settings, each showing its current value.
  void buildRows();
  // Row `row`'s current value: the mode's name, or the setting's chosen value.
  const char* valueOf(size_t row) const;

  static constexpr int8_t MODE_ROW = -1;

  const GameCore::ManifestSettings& settings;
  const uint8_t hostModes;
  GameSaveStore::Choices& choices;
  bool& changed;
  // What each row shows: MODE_ROW, or the index of its setting. Built in onEnter.
  int8_t rowSetting[MAX_ROWS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  size_t rowCount = 0;
};
